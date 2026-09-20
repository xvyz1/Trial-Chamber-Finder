#include "ChamberGenerator.h"
#include "ChamberData.h"
#include "Random.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const TcDataSet *DS = &TC_DATA[0];
static int g_version = -1;
#define TC_TEMPLATES (DS->templates)
#define TC_JIGSAWS (DS->jigsaws)
#define TC_POOLS (DS->pools)
#define TC_POOL_ELEMENTS (DS->pool_elements)
#define TC_NUM_TEMPLATES (DS->num_templates)
#define TC_NUM_POOL_IDS (DS->num_pool_ids)
#define TC_POOL_EMPTY (DS->pool_empty)
#define TC_POOL_START (DS->pool_start)

#define MAX_DEPTH 20
#define MAX_DIST 116
#define MAX_JIG 64
#define MAX_PRIO 8
#define MAX_LIST 2048

const char *tc_mob_name(int group, int i) {
    tc_init();
    if (group == 0) return i >= 0 && i < 3 ? DS->ranged_name[i] : "?";
    if (group == 1) return i >= 0 && i < 3 ? DS->melee_name[i] : "?";
    return i >= 0 && i < 4 ? DS->small_melee_name[i] : "?";
}

int tc_deep_dark_allowed(void) {
    tc_init();
    return DS->deep_dark_ok;
}

int tc_region_blocks(void) {
    tc_init();
    return DS->spacing * 16;
}

static const int DIR_OFF[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

static const unsigned char DIR_ROT[4][6] = {
    {DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST},
    {DIR_DOWN, DIR_UP, DIR_EAST, DIR_WEST, DIR_NORTH, DIR_SOUTH},
    {DIR_DOWN, DIR_UP, DIR_SOUTH, DIR_NORTH, DIR_EAST, DIR_WEST},
    {DIR_DOWN, DIR_UP, DIR_WEST, DIR_EAST, DIR_SOUTH, DIR_NORTH},
};

const char *tc_template_name(int tpl) { return TC_TEMPLATES[tpl].name; }
int tc_num_templates(void) { return TC_NUM_TEMPLATES; }

const char *tc_rotation_name(int rot) {
    static const char *const n[4] = {"NONE", "CLOCKWISE_90", "CLOCKWISE_180", "COUNTERCLOCKWISE_90"};
    return n[rot];
}

static void rotate_pos(int rot, int x, int y, int z, int *ox, int *oy, int *oz) {
    switch (rot) {
    case ROT_CW90:  *ox = -z; *oy = y; *oz = x; break;
    case ROT_180:   *ox = -x; *oy = y; *oz = -z; break;
    case ROT_CCW90: *ox = z;  *oy = y; *oz = -x; break;
    default:        *ox = x;  *oy = y; *oz = z; break;
    }
}

static TcBox template_box(int tpl, int rot, int px, int py, int pz) {
    const TcTemplate *t = &TC_TEMPLATES[tpl];
    int ax, ay, az, bx, by, bz;
    rotate_pos(rot, 0, 0, 0, &ax, &ay, &az);
    rotate_pos(rot, t->sx - 1, t->sy - 1, t->sz - 1, &bx, &by, &bz);
    TcBox b;
    b.minX = (ax < bx ? ax : bx) + px; b.maxX = (ax > bx ? ax : bx) + px;
    b.minY = (ay < by ? ay : by) + py; b.maxY = (ay > by ? ay : by) + py;
    b.minZ = (az < bz ? az : bz) + pz; b.maxZ = (az > bz ? az : bz) + pz;
    return b;
}

static int box_contains(const TcBox *b, int x, int y, int z) {
    return x >= b->minX && x <= b->maxX && y >= b->minY && y <= b->maxY && z >= b->minZ && z <= b->maxZ;
}

static inline int box_inside(const TcBox *in, const TcBox *out) {
    return (in->minX >= out->minX) & (in->maxX <= out->maxX) & (in->minY >= out->minY) & (in->maxY <= out->maxY) &
           (in->minZ >= out->minZ) & (in->maxZ <= out->maxZ);
}

static inline int box_intersects(const TcBox *a, const TcBox *b) {
    return (a->minX <= b->maxX) & (a->maxX >= b->minX) & (a->minY <= b->maxY) & (a->maxY >= b->minY) &
           (a->minZ <= b->maxZ) & (a->maxZ >= b->minZ);
}

static inline int ctz64(uint64_t v) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64)) && !defined(TC_PORTABLE)
    unsigned long i;
    _BitScanForward64(&i, v);
    return (int)i;
#elif (defined(__GNUC__) || defined(__clang__)) && !defined(TC_PORTABLE)
    return __builtin_ctzll(v);
#else
    int i = 0;
    while (!(v & 1)) { v >>= 1; i++; }
    return i;
#endif
}

typedef struct {
    int x, y, z;
    int facing, top, rollable, sel, place, name, target, pool;
} Jig;

static void shuffle_shorts(Rng *rng, short *a, int n) {
    for (int i = n; i > 1; i--) {
        int k = rng_int_fast(rng, i);
        short tmp = a[i - 1]; a[i - 1] = a[k]; a[k] = tmp;
    }
}

typedef struct { short x, y, z; unsigned char facing, top; } RotJig;

uint32_t rng_thr[RNG_FAST_MAX + 1];
uint64_t rng_fm[RNG_FAST_MAX + 1];
static RotJig ROTJIG[4][TC_MAX_JIGSAWS];
static TcBox TBOX[TC_MAX_TEMPLATES][4];
static unsigned char HAS_SEL[TC_MAX_TEMPLATES];

static uint64_t MASK_NAME[TC_MAX_TEMPLATES][TC_MAX_NAMES];
static uint64_t MASK_FACING[TC_MAX_TEMPLATES][4][6], MASK_TOP[TC_MAX_TEMPLATES][4][6];
static int g_init;
static void jump_init(void);
static void fseq_init(void);
static void orbit_init(void);
static void cand_init(void);
static void rng_jump(Rng *r, uint64_t L);

void rng_fast_init(void) {
    for (int n = 1; n <= RNG_FAST_MAX; n++) {
        rng_thr[n] = (uint32_t)((((uint64_t)1 << 31) / (uint64_t)n) * (uint64_t)n);
        rng_fm[n] = 0xFFFFFFFFFFFFFFFFULL / (uint64_t)n + 1;
    }
}

static void tables_init(void) {
    memset(MASK_NAME, 0, sizeof MASK_NAME);
    memset(MASK_FACING, 0, sizeof MASK_FACING);
    memset(MASK_TOP, 0, sizeof MASK_TOP);
    for (int t = 0; t < TC_NUM_TEMPLATES; t++) {
        const TcTemplate *tp = &TC_TEMPLATES[t];
        for (int rot = 0; rot < 4; rot++) {
            TBOX[t][rot] = template_box(t, rot, 0, 0, 0);
            for (int i = 0; i < tp->jig_n; i++) {
                const TcJigsaw *j = &TC_JIGSAWS[tp->jig_off + i];
                RotJig *o = &ROTJIG[rot][tp->jig_off + i];
                int x, y, z;
                rotate_pos(rot, j->x, j->y, j->z, &x, &y, &z);
                o->x = (short)x; o->y = (short)y; o->z = (short)z;
                o->facing = DIR_ROT[rot][j->facing];
                o->top = DIR_ROT[rot][j->top];
            }
        }
        for (int i = 0; i < tp->jig_n; i++) {
            const TcJigsaw *j = &TC_JIGSAWS[tp->jig_off + i];
            MASK_NAME[t][j->name] |= (uint64_t)1 << i;
            for (int rot = 0; rot < 4; rot++) {
                MASK_FACING[t][rot][DIR_ROT[rot][j->facing]] |= (uint64_t)1 << i;
                MASK_TOP[t][rot][DIR_ROT[rot][j->top]] |= (uint64_t)1 << i;
            }
        }
        HAS_SEL[t] = 0;
        for (int i = 1; i < tp->jig_n; i++) {
            if (TC_JIGSAWS[tp->jig_off + i].sel != TC_JIGSAWS[tp->jig_off].sel) HAS_SEL[t] = 1;
        }
    }
    fseq_init();
    cand_init();
}

int tc_num_versions(void) { return TC_NUM_VERSIONS; }
const char *tc_version_name(int v) { return v >= 0 && v < TC_NUM_VERSIONS ? TC_VERSION_NAMES[v] : NULL; }

int tc_version_index(const char *name) {
    for (int v = 0; v < TC_NUM_VERSIONS; v++) if (strcmp(TC_VERSION_NAMES[v], name) == 0) return v;
    for (int v = 0; v < TC_NUM_COVERED; v++) if (strcmp(TC_COVERED_NAMES[v], name) == 0) return TC_COVERED_ENTRY[v];
    return -1;
}

void tc_select_version(int v) {
    if (!g_init) {
        rng_fast_init();
        jump_init();
        orbit_init();
        g_init = 1;
    }
    if (v < 0 || v >= TC_NUM_VERSIONS) v = 0;
    const TcDataSet *ds = &TC_DATA[TC_VERSION_DATA[v]];
    if (g_version < 0 || ds != DS) { DS = ds; tables_init(); }
    g_version = v;
}

int tc_selected_version(void) {
    tc_init();
    return g_version;
}

void tc_init(void) {
    if (g_version < 0) {
        int v = tc_version_index("1.21.1");
        tc_select_version(v < 0 ? 0 : v);
    }
}

#define JUMP_MAX 256
#define LIMIT_STATE ((uint64_t)(((uint64_t)1 << 31) - RNG_FAST_MAX) << 17)
static uint64_t JA[JUMP_MAX + 1], JC[JUMP_MAX + 1];
static uint64_t JS[JUMP_MAX + 1], JT[JUMP_MAX + 1];
static uint64_t JPA[48], JPC[48];
static short DESC[RNG_FAST_MAX + 1];
static short FSEQ[TC_MAX_TEMPLATES][3 + 4 * (MAX_JIG - 1)];
static short FLEN[TC_MAX_TEMPLATES];

static void jump_init(void) {
    JA[0] = 1; JC[0] = 0;
    for (int i = 1; i <= JUMP_MAX; i++) {
        JA[i] = (JA[i - 1] * RNG_MUL) & RNG_MASK;
        JC[i] = (JC[i - 1] * RNG_MUL + 0xB) & RNG_MASK;
    }
    JPA[0] = RNG_MUL; JPC[0] = 0xB;
    for (int k = 1; k < 48; k++) {
        JPC[k] = (JPA[k - 1] * JPC[k - 1] + JPC[k - 1]) & RNG_MASK;
        JPA[k] = (JPA[k - 1] * JPA[k - 1]) & RNG_MASK;
    }
    for (int i = 0; i <= JUMP_MAX; i++) {
        JS[i] = JA[i] << 16;
        JT[i] = (JC[i] << 16) + ((uint64_t)1 << 44);
    }
    for (int i = 0; i <= RNG_FAST_MAX; i++) DESC[i] = (short)(RNG_FAST_MAX - i);
}

static void fseq_init(void) {
    for (int t = 0; t < TC_NUM_TEMPLATES; t++) {
        int n = TC_TEMPLATES[t].jig_n, k = 0;
        FSEQ[t][k++] = 4; FSEQ[t][k++] = 3; FSEQ[t][k++] = 2;
        for (int r = 0; r < 4; r++)
            for (int j = n; j > 1; j--) FSEQ[t][k++] = (short)j;
        FLEN[t] = (short)k;
    }
}

static int any_candidate(uint64_t s0, int len) {
    uint64_t m0 = ~0ULL, m1 = ~0ULL, m2 = ~0ULL, m3 = ~0ULL;
    int i = 1;
    for (; i + 3 <= len; i += 4) {
        uint64_t y0 = JS[i] * s0 + JT[i], y1 = JS[i + 1] * s0 + JT[i + 1];
        uint64_t y2 = JS[i + 2] * s0 + JT[i + 2], y3 = JS[i + 3] * s0 + JT[i + 3];
        m0 = y0 < m0 ? y0 : m0; m1 = y1 < m1 ? y1 : m1;
        m2 = y2 < m2 ? y2 : m2; m3 = y3 < m3 ? y3 : m3;
    }
    for (; i <= len; i++) { uint64_t y = JS[i] * s0 + JT[i]; m0 = y < m0 ? y : m0; }
    m0 = m0 < m1 ? m0 : m1; m2 = m2 < m3 ? m2 : m3;
    return (m0 < m2 ? m0 : m2) < ((uint64_t)1 << 44);
}

static void rng_skip_seq(Rng *r, const short *seq, int K) {
    uint64_t s0 = r->s;
    int start = 0;
    while (start < K) {
        int len = K - start;
        if (len > JUMP_MAX) len = JUMP_MAX;
        int hit = 0;
        if (any_candidate(s0, len)) {
            for (int i = 1; i <= len; i++) {
                uint64_t si = (JA[i] * s0 + JC[i]) & RNG_MASK;
                if (si >= LIMIT_STATE) {
                    int j = seq[start + i - 1];
                    if ((j & -j) != j && (uint32_t)(si >> 17) >= rng_thr[j]) { hit = i; break; }
                }
            }
        }
        if (!hit) {
            s0 = (JA[len] * s0 + JC[len]) & RNG_MASK;
            r->n += (uint64_t)len;
            start += len;
        } else {
            s0 = (JA[hit] * s0 + JC[hit]) & RNG_MASK;
            r->n += (uint64_t)hit;
            start += hit - 1;
        }
    }
    r->s = s0;
}

#define M48 (((uint64_t)1 << 48) - 1)
#define M50 (((uint64_t)1 << 50) - 1)
static uint64_t AINV_POW[48], INV11;
#define CAND_BUCKETS 65536
static uint32_t *CAND_LO;
static uint32_t CAND_START[CAND_BUCKETS + 1];
static size_t NCAND;
static int CAND_M;

static uint64_t inv_odd64(uint64_t a) {
    uint64_t x = a;
    for (int i = 0; i < 6; i++) x *= 2 - a * x;
    return x;
}

static uint64_t lcg_pos_slow(uint64_t s) {
    uint64_t y = ((((RNG_MUL - 1) * s + 0xB) & M50) * INV11) & M50;
    uint64_t k = 0;
    for (int i = 0; i < 48; i++) {
        if ((y >> (i + 2)) & 1) { y = (y * AINV_POW[i]) & M50; k |= (uint64_t)1 << i; }
    }
    return k;
}

static unsigned short DL1[65536], DL2[65536], DL3[65536];
static uint64_t DLINV1[65536], DLINV2[65536];

static void dlog_init(void) {
    uint64_t a = RNG_MUL, b = a, c;
    for (int i = 0; i < 16; i++) b = (b * b) & M50;
    c = b;
    for (int i = 0; i < 16; i++) c = (c * c) & M50;
    uint64_t pa = 1, pb = 1, pc = 1;
    uint64_t ia = AINV_POW[0], ib = AINV_POW[16];
    uint64_t qa = 1, qb = 1;
    for (int m = 0; m < 65536; m++) {
        DL1[(pa >> 2) & 0xFFFF] = (unsigned short)m;
        DL2[(pb >> 18) & 0xFFFF] = (unsigned short)m;
        DL3[(pc >> 34) & 0xFFFF] = (unsigned short)m;
        DLINV1[m] = qa;
        DLINV2[m] = qb;
        pa = (pa * a) & M50; pb = (pb * b) & M50; pc = (pc * c) & M50;
        qa = (qa * ia) & M50; qb = (qb * ib) & M50;
    }
}

static inline uint64_t lcg_pos(uint64_t s) {
    uint64_t y = ((((RNG_MUL - 1) * s + 0xB) & M50) * INV11) & M50;
    uint64_t m1 = DL1[(y >> 2) & 0xFFFF];
    y = (y * DLINV1[m1]) & M50;
    uint64_t m2 = DL2[(y >> 18) & 0xFFFF];
    y = (y * DLINV2[m2]) & M50;
    uint64_t m3 = DL3[(y >> 34) & 0xFFFF];
    return m1 | (m2 << 16) | (m3 << 32);
}

static uint64_t lcg_state_at(uint64_t k) {
    uint64_t ra = 1, rc = 0, ba = RNG_MUL, bc = 0xB;
    while (k) {
        if (k & 1) { rc = (ba * rc + bc) & M48; ra = (ba * ra) & M48; }
        bc = (ba * bc + bc) & M48;
        ba = (ba * ba) & M48;
        k >>= 1;
    }
    return rc;
}

static void sort_u32(uint32_t *a, int n) {
    while (n > 16) {
        uint32_t p = a[n / 2];
        int i = 0, j = n - 1;
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { uint32_t t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j + 1 < n - i) { sort_u32(a, j + 1); a += i; n -= i; }
        else { sort_u32(a + i, n - i); n = j + 1; }
    }
    for (int i = 1; i < n; i++) {
        uint32_t v = a[i];
        int k = i - 1;
        while (k >= 0 && a[k] > v) { a[k + 1] = a[k]; k--; }
        a[k + 1] = v;
    }
}

static void orbit_init(void) {
    uint64_t ainv = inv_odd64(RNG_MUL) & M50;
    AINV_POW[0] = ainv;
    for (int i = 1; i < 48; i++) AINV_POW[i] = (AINV_POW[i - 1] * AINV_POW[i - 1]) & M50;
    INV11 = inv_odd64(0xB) & M50;
    dlog_init();
}

static void cand_init(void) {
    int maxb = 4;
    for (int t = 0; t < TC_NUM_TEMPLATES; t++) if (TC_TEMPLATES[t].jig_n > maxb) maxb = TC_TEMPLATES[t].jig_n;
    int m = 1;
    for (int j = 2; j <= maxb; j++) {
        if ((j & -j) == j) continue;
        int rem = (int)((((uint64_t)1) << 31) % (uint64_t)j);
        if (rem > m) m = rem;
    }
    if (CAND_LO && m == CAND_M) return;
    free(CAND_LO);
    CAND_M = m;
    NCAND = (size_t)CAND_M << 17;
    CAND_LO = (uint32_t *)malloc(sizeof(uint32_t) * NCAND);
    uint32_t *fill = (uint32_t *)malloc(sizeof(uint32_t) * CAND_BUCKETS);
    if (!CAND_LO || !fill) { fprintf(stderr, "Not enough memory (needs about 30 MB)\n"); exit(1); }
    uint64_t first = (((uint64_t)1 << 31) - (uint64_t)CAND_M) << 17;
    memset(CAND_START, 0, sizeof CAND_START);
    for (size_t i = 0; i < NCAND; i++) CAND_START[(lcg_pos(first + i) >> 32) + 1]++;
    for (int b = 0; b < CAND_BUCKETS; b++) CAND_START[b + 1] += CAND_START[b];
    memcpy(fill, CAND_START, sizeof(uint32_t) * CAND_BUCKETS);
    for (size_t i = 0; i < NCAND; i++) {
        uint64_t k = lcg_pos(first + i);
        CAND_LO[fill[k >> 32]++] = (uint32_t)k;
    }
    free(fill);
    for (int b = 0; b < CAND_BUCKETS; b++) sort_u32(CAND_LO + CAND_START[b], (int)(CAND_START[b + 1] - CAND_START[b]));
}

static uint64_t cand_get(size_t i) {
    uint32_t lo = 0, hi = CAND_BUCKETS;
    while (lo + 1 < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (CAND_START[mid] <= i) lo = mid; else hi = mid;
    }
    return ((uint64_t)lo << 32) | CAND_LO[i];
}

static uint64_t cand_dist_after(uint64_t q) {
    uint32_t b = (uint32_t)(q >> 32), lo = (uint32_t)q;
    uint32_t i = CAND_START[b], j = CAND_START[b + 1];
    while (i < j) {
        uint32_t mid = (i + j) / 2;
        if (CAND_LO[mid] <= lo) i = mid + 1; else j = mid;
    }
    while (i == CAND_START[b + 1]) {
        if (++b == CAND_BUCKETS) return cand_get(0) + ((uint64_t)1 << 48) - q;
        i = CAND_START[b];
    }
    return (((uint64_t)b << 32) | CAND_LO[i]) - q;
}

static void shuffle_shorts2(Rng *r, short *a, int n) {
    if (n < 2) return;
    uint64_t A2 = JA[2], C2 = JC[2];
    uint64_t s1 = (RNG_MUL * r->s + 0xB) & RNG_MASK;
    uint64_t s2 = (A2 * r->s + C2) & RNG_MASK;
    uint64_t used = 0, last = r->s;
    for (int i = n; i > 1; i--) {
        int32_t bits;
        for (;;) {
            uint64_t st = s1;
            last = st;
            s1 = s2;
            s2 = (A2 * st + C2) & RNG_MASK;
            used++;
            bits = (int32_t)(st >> 17);
            if ((i & -i) == i) { bits = (int32_t)(((int64_t)i * bits) >> 31); break; }
            if ((uint32_t)bits < rng_thr[i]) { bits = (int32_t)fastmod_u32((uint32_t)bits, rng_fm[i], (uint32_t)i); break; }
        }
        short tmp = a[i - 1]; a[i - 1] = a[bits]; a[bits] = tmp;
    }
    r->s = last;
    r->n += used;
}

typedef struct {
    TcBox container;
    int head;
} Shape;

typedef struct {
    int piece, shape, depth;
} QEntry;

typedef struct {
    Rng rng;
    TcChamber *c;
    Shape shapes[TC_MAX_PIECES + 1];
    int nshapes;
    struct { TcBox box; int next; } hole[TC_MAX_PIECES];
    QEntry queue[MAX_PRIO][TC_MAX_PIECES];
    int qhead[MAX_PRIO], qtail[MAX_PRIO];
    short alias[TC_MAX_POOL_IDS];
    int overflow;
    unsigned stamp;
    unsigned failed[TC_MAX_TEMPLATES];
    uint64_t kbase;
    uint64_t cand_n;
} Work;

static void skip_seq(Work *w, const short *seq, int K) {
    Rng *r = &w->rng;
    if (K > JUMP_MAX) { rng_skip_seq(r, seq, K); return; }
    int start = 0;
    while (start < K) {
        if (r->n >= w->cand_n)
            w->cand_n = r->n + cand_dist_after((w->kbase + r->n) & M48);
        uint64_t d = w->cand_n - r->n;
        int len = K - start;
        if (d > (uint64_t)len) {
            r->s = (JA[len] * r->s + JC[len]) & RNG_MASK;
            r->n += (uint64_t)len;
            return;
        }
        int di = (int)d;
        uint64_t sd = (JA[di] * r->s + JC[di]) & RNG_MASK;
        int j = seq[start + di - 1];
        r->s = sd;
        r->n += (uint64_t)di;
        if ((j & -j) != j && (uint32_t)(sd >> 17) >= rng_thr[j]) { start += di - 1; }
        else start += di;
    }
}

int tc_selftest(long trials) {
    tc_init();
    int fails = 0;
    uint64_t x = 0x9E3779B97F4A7C15ULL;
#define RND() (x ^= x << 13, x ^= x >> 7, x ^= x << 17, x)
    for (int i = 0; i < 100000; i++) {
        uint64_t s = RND() & M48, k = lcg_pos(s);
        uint64_t s1 = (s * RNG_MUL + 0xB) & M48;
        if (((lcg_pos(s1) - k) & M48) != 1 || lcg_state_at(k) != s || lcg_pos_slow(s) != k) { if (fails++ < 5) printf("  position math wrong for %" PRIx64 "\n", s); }
    }
    for (int i = 0; i < 10000; i++) {
        uint64_t k = cand_get(RND() % NCAND);
        if ((lcg_state_at(k) >> 17) < ((uint64_t)1 << 31) - (uint64_t)CAND_M) { if (fails++ < 5) printf("  listed state not reroll-capable\n"); }
    }
    for (long i = 0; i < 4000000; i++) {
        int nb = 1 + (int)(RND() % RNG_FAST_MAX);
        uint64_t s = RND() & M48;
        if (i & 1) s = (s & ((1ULL << 17) - 1)) | ((((uint64_t)1 << 31) - 1 - RND() % 4096) << 17);
        Rng p = {s, 0}, q = {s, 0};
        if (rng_int_fast(&p, nb) != rng_next_int(&q, nb) || p.s != q.s || p.n != q.n) { if (fails++ < 5) printf("  nextInt mismatch bound %d\n", nb); }
    }
    for (int i = 0; i < 2000; i++) {
        uint64_t L = RND() % 300000, s = RND() & M48;
        Rng p = {s, 0}, q = {s, 0};
        rng_jump(&p, L);
        for (uint64_t k = 0; k < L; k++) rng_next31(&q);
        if (p.s != q.s || p.n != q.n) { if (fails++ < 5) printf("  jump mismatch L %" PRIu64 "\n", L); }
    }
    Work *w = (Work *)malloc(sizeof(Work));
    long rerolls = 0;
    for (long i = 0; i < trials; i++) {
        int t = (int)(RND() % TC_NUM_TEMPLATES);
        const short *seq;
        int K;
        if (RND() & 1) { seq = FSEQ[t]; K = FLEN[t]; }
        else { int ne = TC_TEMPLATES[t].jig_n; seq = DESC + (RNG_FAST_MAX - ne); K = ne - 1; }
        if (K <= 0) continue;
        uint64_t s0;
        if (RND() & 1) s0 = RND() & M48;
        else {
            uint64_t near = cand_get(RND() % NCAND);
            uint64_t back = RND() % (uint64_t)K;
            s0 = lcg_state_at((near - 1 - back) & M48);
        }
        Rng a = {s0, 0}, b = {s0, 0};
        rng_skip_seq(&a, seq, K);
        w->rng = b;
        w->kbase = lcg_pos(s0);
        w->cand_n = cand_dist_after(w->kbase);
        skip_seq(w, seq, K);

        Rng c = {s0, 0};
        for (int q = 0; q < K; q++) {
            int j = seq[q];
            if ((j & -j) == j) { rng_next31(&c); continue; }
            while ((uint32_t)rng_next31(&c) >= rng_thr[j]) rerolls++;
        }
        if (a.s != w->rng.s || c.s != a.s || w->rng.n != c.n) { if (fails++ < 5) printf("  replay mismatch (template %d, K %d)\n", t, K); }
    }
    free(w);
    printf("selftest: %ld replay trials (%ld rerolls inside them), %lu listed states (M %d), %d failures\n", trials, rerolls,
           (unsigned long)NCAND, CAND_M, fails);
#undef RND
    return fails;
}

static void rng_jump(Rng *r, uint64_t L) {
    uint64_t s = r->s;
    r->n += L;
    for (int k = 0; L; k++, L >>= 1)
        if (L & 1) s = (JPA[k] * s + JPC[k]) & RNG_MASK;
    r->s = s;
}

static void flush_failed(Work *w, const short *pend, int npend, uint64_t steps) {
    Rng *r = &w->rng;
    if (r->n >= w->cand_n) w->cand_n = r->n + cand_dist_after((w->kbase + r->n) & M48);
    if (w->cand_n - r->n > steps) { rng_jump(r, steps); return; }
    for (int i = 0; i < npend; i++) skip_seq(w, FSEQ[pend[i]], FLEN[pend[i]]);
}

struct TcWork { Work w; };

TcWork *tc_work_new(void) {
    tc_init();
    TcWork *tw = (TcWork *)malloc(sizeof(TcWork));
    if (tw) { tw->w.stamp = 0; memset(tw->w.failed, 0, sizeof tw->w.failed); }
    return tw;
}

void tc_work_free(TcWork *tw) { free(tw); }

static int new_shape(Work *w, TcBox container) {
    Shape *s = &w->shapes[w->nshapes];
    s->container = container;
    s->head = -1;
    return w->nshapes++;
}

static int shape_fits(const Work *w, int shape, const TcBox *b) {
    const Shape *s = &w->shapes[shape];
    if (!box_inside(b, &s->container)) return 0;
    for (int h = s->head; h >= 0; h = w->hole[h].next) {
        if (box_intersects(b, &w->hole[h].box)) return 0;
    }
    return 1;
}

static void shape_subtract(Work *w, int shape, int piece) {
    w->hole[piece].box = w->c->pieces[piece].box;
    w->hole[piece].next = w->shapes[shape].head;
    w->shapes[shape].head = piece;
}

static int add_piece(Work *w, int tpl, int rot, int x, int y, int z, TcBox box, int depth) {
    TcChamber *c = w->c;
    if (c->npieces >= TC_MAX_PIECES) { w->overflow = 1; return -1; }
    TcPiece *p = &c->pieces[c->npieces];
    p->tpl = tpl; p->rot = rot; p->x = x; p->y = y; p->z = z; p->box = box; p->depth = depth;
    return c->npieces++;
}

static void enqueue(Work *w, int piece, int shape, int depth, int prio) {
    if (prio < 0 || prio >= MAX_PRIO) { w->overflow = 1; return; }
    QEntry *e = &w->queue[prio][w->qtail[prio]++];
    e->piece = piece; e->shape = shape; e->depth = depth;
}

static int dequeue(Work *w, QEntry *out) {
    for (int p = MAX_PRIO - 1; p >= 0; p--) {
        if (w->qhead[p] < w->qtail[p]) { *out = w->queue[p][w->qhead[p]++]; return 1; }
    }
    return 0;
}

static int element_jigsaws(Rng *rng, int tpl, int rot, int px, int py, int pz, Jig *out) {
    const TcTemplate *t = &TC_TEMPLATES[tpl];
    int n = t->jig_n;
    unsigned char idx[MAX_JIG];
    for (int i = 0; i < n; i++) idx[i] = (unsigned char)i;
    for (int i = n; i > 1; i--) {
        int k = rng_int_fast(rng, i);
        unsigned char tmp = idx[i - 1]; idx[i - 1] = idx[k]; idx[k] = tmp;
    }
    const TcJigsaw *tj = &TC_JIGSAWS[t->jig_off];
    if (HAS_SEL[tpl]) {
        for (int i = 1; i < n; i++) {
            unsigned char v = idx[i];
            int k = i - 1;
            while (k >= 0 && tj[idx[k]].sel < tj[v].sel) { idx[k + 1] = idx[k]; k--; }
            idx[k + 1] = v;
        }
    }
    const RotJig *rj = &ROTJIG[rot][t->jig_off];
    for (int i = 0; i < n; i++) {
        const TcJigsaw *j = &tj[idx[i]];
        const RotJig *r = &rj[idx[i]];
        Jig *o = &out[i];
        o->x = r->x + px; o->y = r->y + py; o->z = r->z + pz;
        o->facing = r->facing; o->top = r->top;
        o->rollable = j->rollable; o->sel = j->sel; o->place = j->place;
        o->name = j->name; o->target = j->target; o->pool = j->pool;
    }
    return n;
}

static void generate_piece(Work *w, int pi, int shape_ref, int depth) {
    Jig jigs[MAX_JIG];
    short list[MAX_LIST], pend[MAX_LIST];
    TcPiece piece = w->c->pieces[pi];
    int nj = element_jigsaws(&w->rng, piece.tpl, piece.rot, piece.x, piece.y, piece.z, jigs);
    int inner = -1;
    int min_y = piece.box.minY;

    for (int ai = 0; ai < nj; ai++) {
        const Jig *a = &jigs[ai];
        if (a->pool < 0) continue;
        int pool_id = w->alias[a->pool];
        const TcPool *pool = &TC_POOLS[pool_id];
        const TcPool *fb = &TC_POOLS[pool->fallback];
        if (pool->n == 0 && pool_id != TC_POOL_EMPTY) continue;
        if (fb->n == 0 && pool->fallback != TC_POOL_EMPTY) continue;

        int tx = a->x + DIR_OFF[a->facing][0], ty = a->y + DIR_OFF[a->facing][1], tz = a->z + DIR_OFF[a->facing][2];
        int shape;
        if (box_contains(&piece.box, tx, ty, tz)) {
            if (inner < 0) inner = new_shape(w, piece.box);
            shape = inner;
        } else {
            shape = shape_ref;
        }

        int n = 0;
        if (depth != MAX_DEPTH) {
            memcpy(list, &TC_POOL_ELEMENTS[pool->off], sizeof(short) * pool->n);
            if (pool->n >= 16) shuffle_shorts2(&w->rng, list, pool->n); else shuffle_shorts(&w->rng, list, pool->n);
            n = pool->n;
        }
        memcpy(list + n, &TC_POOL_ELEMENTS[fb->off], sizeof(short) * fb->n);
        shuffle_shorts(&w->rng, list + n, fb->n);
        n += fb->n;

        int j_rel = a->y - min_y;
        int want_facing = a->facing ^ 1;
        if (++w->stamp == 0) {
            memset(w->failed, 0, sizeof w->failed);
            w->stamp = 1;
        }
        int npend = 0;
        uint64_t psteps = 0;
        for (int ei = 0; ei < n; ei++) {
            int e = list[ei];
            if (e < 0) break;
            if (w->failed[e] == w->stamp) {
                pend[npend++] = (short)e;
                psteps += (uint64_t)FLEN[e];
                continue;
            }
            if (npend) { flush_failed(w, pend, npend, psteps); npend = 0; psteps = 0; }
            const TcTemplate *et = &TC_TEMPLATES[e];
            int ne = et->jig_n;
            short rots[4] = {ROT_NONE, ROT_CW90, ROT_180, ROT_CCW90};
            shuffle_shorts(&w->rng, rots, 4);
            for (int ri = 0; ri < 4; ri++) {
                int rot = rots[ri];
                const RotJig *rj = &ROTJIG[rot][et->jig_off];
                const TcJigsaw *ej = &TC_JIGSAWS[et->jig_off];
                uint64_t mask = MASK_NAME[e][a->target] & MASK_FACING[e][rot][want_facing];
                if (!a->rollable) mask &= MASK_TOP[e][rot][a->top];
                if (!mask) { skip_seq(w, DESC + (RNG_FAST_MAX - ne), ne - 1); continue; }

                unsigned char ord[MAX_JIG];
                int nm = 0;
                if ((mask & (mask - 1)) == 0) {
                    skip_seq(w, DESC + (RNG_FAST_MAX - ne), ne - 1);
                    ord[nm++] = (unsigned char)ctz64(mask);
                } else {
                    unsigned char idx[MAX_JIG];
                    for (int i = 0; i < ne; i++) idx[i] = (unsigned char)i;
                    for (int i = ne; i > 1; i--) {
                        int k = rng_int_fast(&w->rng, i);
                        unsigned char tmp = idx[i - 1]; idx[i - 1] = idx[k]; idx[k] = tmp;
                    }
                    if (HAS_SEL[e]) {
                        for (int i = 1; i < ne; i++) {
                            unsigned char v = idx[i];
                            int k = i - 1;
                            while (k >= 0 && ej[idx[k]].sel < ej[v].sel) { idx[k + 1] = idx[k]; k--; }
                            idx[k + 1] = v;
                        }
                    }
                    for (int i = 0; i < ne; i++) if ((mask >> idx[i]) & 1) ord[nm++] = idx[i];
                }
                TcBox ebox = TBOX[e][rot];
                for (int bi = 0; bi < nm; bi++) {
                    int i = ord[bi];
                    const RotJig *b = &rj[i];
                    int ox = tx - b->x, oy = ty - b->y, oz = tz - b->z;
                    TcBox box3 = ebox;
                    box3.minX += ox; box3.maxX += ox; box3.minY += oy; box3.maxY += oy; box3.minZ += oz; box3.maxZ += oz;
                    int p = j_rel - b->y + DIR_OFF[a->facing][1];
                    int q = min_y + p;
                    int r = q - box3.minY;
                    box3.minY += r; box3.maxY += r;
                    if (!shape_fits(w, shape, &box3)) continue;
                    int child = add_piece(w, e, rot, ox, oy + r, oz, box3, depth + 1);
                    if (child < 0) return;
                    shape_subtract(w, shape, child);
                    if (depth + 1 <= MAX_DEPTH) enqueue(w, child, shape, depth + 1, a->place);
                    goto next_jigsaw;
                }
            }
            w->failed[e] = w->stamp;
        }
        if (npend) flush_failed(w, pend, npend, psteps);
    next_jigsaw:;
    }
}

static int64_t pos_hash(int x, int y, int z) {
    int64_t l = (int64_t)(int32_t)((uint32_t)x * 3129871u) ^ jmul(z, 116129781LL) ^ (int64_t)y;
    l = (int64_t)((uint64_t)l * (uint64_t)l * 42317861ULL + (uint64_t)l * 11ULL);
    return l >= 0 ? l >> 16 : ~((~l) >> 16);
}

static void choose_aliases(Work *w, int64_t seed, int x, int y, int z) {
    Rng r;
    rng_set_seed(&r, seed);
    int64_t split = rng_next_long(&r);
    rng_set_seed(&r, pos_hash(x, y, z) ^ split);
    TcChamber *c = w->c;
    c->ranged = rng_next_int(&r, 3);
    c->melee = rng_next_int(&r, 3);
    c->small_melee = rng_next_int(&r, 4);

    for (int i = 0; i < TC_NUM_POOL_IDS; i++) w->alias[i] = (short)i;
    w->alias[DS->alias_ranged] = DS->ranged[c->ranged];
    w->alias[DS->alias_slow_ranged] = DS->slow_ranged[c->ranged];
    w->alias[DS->alias_melee] = DS->melee[c->melee];
    w->alias[DS->alias_small_melee] = DS->small_melee[c->small_melee];
}

void tc_candidate(int64_t seed, int rx, int rz, int *chunkX, int *chunkZ) {
    Rng r;
    tc_init();
    int sp = DS->spacing, off = DS->spacing - DS->separation;
    rng_set_seed(&r, jmul(rx, 341873128712LL) + jmul(rz, 132897987541LL) + seed + 94251327LL);
    *chunkX = rx * sp + rng_next_int(&r, off);
    *chunkZ = rz * sp + rng_next_int(&r, off);
}

typedef struct {
    int start_y, rot, tpl;
    TcBox box;
    int cx, cz;
} Start;

static void start_piece(Rng *rng, int64_t seed, int chunkX, int chunkZ, Start *s) {
    rng_set_seed(rng, seed);
    int64_t a = rng_next_long(rng);
    int64_t b = rng_next_long(rng);
    rng_set_seed(rng, jmul(chunkX, a) ^ jmul(chunkZ, b) ^ seed);
    s->start_y = -40 + rng_next_int(rng, 21);
    s->rot = rng_next_int(rng, 4);
    const TcPool *start_pool = &TC_POOLS[TC_POOL_START];
    s->tpl = TC_POOL_ELEMENTS[start_pool->off + rng_next_int(rng, start_pool->n)];
    s->box = template_box(s->tpl, s->rot, chunkX * 16, s->start_y, chunkZ * 16);
    s->cx = (s->box.minX + s->box.maxX) / 2;
    s->cz = (s->box.minZ + s->box.maxZ) / 2;
}

void tc_start_pos(int64_t seed, int chunkX, int chunkZ, int *posX, int *posY, int *posZ) {
    Rng rng;
    Start s;
    start_piece(&rng, seed, chunkX, chunkZ, &s);
    *posX = s.cx; *posY = s.start_y; *posZ = s.cz;
}

int tc_generate(TcChamber *c, int64_t seed, int chunkX, int chunkZ) {
    TcWork *tw = tc_work_new();
    if (!tw) return -1;
    int r = tc_generate_w(tw, c, seed, chunkX, chunkZ);
    tc_work_free(tw);
    return r;
}

int tc_generate_w(TcWork *tw, TcChamber *c, int64_t seed, int chunkX, int chunkZ) {
    Work *w = &tw->w;
    memset(w->qhead, 0, sizeof w->qhead);
    memset(w->qtail, 0, sizeof w->qtail);
    w->nshapes = 0;
    w->overflow = 0;
    w->c = c;
    c->seed = seed; c->chunkX = chunkX; c->chunkZ = chunkZ;
    c->npieces = 0;

    Start s;
    start_piece(&w->rng, seed, chunkX, chunkZ, &s);
    w->kbase = (lcg_pos(w->rng.s) - w->rng.n) & M48;
    w->cand_n = w->rng.n + cand_dist_after((w->kbase + w->rng.n) & M48);
    int start_y = s.start_y, cx = s.cx, cz = s.cz;
    int sx = chunkX * 16, sz = chunkZ * 16;
    choose_aliases(w, seed, sx, start_y, sz);
    TcBox box = s.box;

    box.minY -= 1; box.maxY -= 1;
    int start = add_piece(w, s.tpl, s.rot, sx, start_y - 1, sz, box, 0);

    c->startY = start_y;
    c->posX = cx; c->posY = start_y; c->posZ = cz;

    TcBox outer;
    outer.minX = cx - MAX_DIST; outer.maxX = cx + MAX_DIST;
    outer.minZ = cz - MAX_DIST; outer.maxZ = cz + MAX_DIST;
    outer.minY = start_y - MAX_DIST > DS->min_y ? start_y - MAX_DIST : DS->min_y;
    outer.maxY = start_y + MAX_DIST < DS->max_y ? start_y + MAX_DIST : DS->max_y;
    int outer_shape = new_shape(w, outer);
    shape_subtract(w, outer_shape, start);

    generate_piece(w, start, outer_shape, 0);
    QEntry e;
    while (!w->overflow && dequeue(w, &e)) {
        generate_piece(w, e.piece, e.shape, e.depth);
    }

    c->spawners = c->vaults = c->ominous = 0;
    c->nblocks = 0;
    for (int i = 0; i < c->npieces; i++) {
        const TcPiece *p = &c->pieces[i];
        const TcTemplate *t = &TC_TEMPLATES[p->tpl];
        if (t->kind == KIND_NONE) continue;
        if (t->kind == KIND_SPAWNER) c->spawners++;
        else if (t->kind == KIND_VAULT) c->vaults++;
        else { c->vaults++; c->ominous++; }
        if (c->nblocks < TC_MAX_BLOCKS) {
            TcBlock *b = &c->blocks[c->nblocks++];
            rotate_pos(p->rot, t->bx, t->by, t->bz, &b->x, &b->y, &b->z);
            b->x += p->x; b->y += p->y; b->z += p->z;
            b->kind = t->kind;
            b->tpl = p->tpl;
        } else {
            w->overflow = 1;
        }
    }
    return w->overflow ? -1 : 0;
}
