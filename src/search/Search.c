#include "Search.h"
#include "../util/Threads.h"
#include "../chambergenerator/ChamberGenerator.h"
#include "../chambergenerator/ChamberTypes.h"
#include "../cubiomes/biomenoise.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

static int REGION_BLOCKS = 34 * 16;
#define CHAMBER_REACH 116
#define ANCHOR_CHUNK 64
#define GROUP_HALO 8
#ifndef TILE_BYTES
#define TILE_BYTES (256 * 1024 * 1024)
#endif
#define TILE_MIN 32

const char *const COUNT_NAMES[COUNT_KINDS] = {"TRIAL_SPAWNERS", "VAULTS", "OMINOUS_VAULTS"};
static const char *const COUNT_WORDS[COUNT_KINDS] = {"trial spawners", "vaults", "ominous vaults"};
const char *const SHAPE_NAMES[SHAPE_COUNT] = {"CHAMBERS", "SQUARE", "RECTANGLE", "CIRCLE"};
const char *const SORT_NAMES[SORT_COUNT] = {"AMOUNT", "CLOSENESS", "AMOUNT_THEN_CLOSENESS"};

static int SORT_MODE = SORT_AMOUNT;
static int SORT_BY_SPAN = 0;

#define TC_NUM_TEMPLATES_MAX 1024

typedef struct { int x, z, ch; } Pt;

typedef struct { signed char dx, dz; } Off;

typedef struct { int x, z; short kind, mob; } Block;

typedef struct {
    int rx, rz;
    int posX, posY, posZ;
    int npts;
    Off *pts;
} Ch;

typedef struct {
    int count, nchambers;
    int x, z;
    int shape_w, shape_h;
    int minX, minZ;
    unsigned hash;
    int arx, arz;
    int mem[4][2];
    int64_t span;
} Result;

static int MOB_OF[TC_NUM_TEMPLATES_MAX];

static int cmp_x(const void *a, const void *b) { return ((const Pt *)a)->x - ((const Pt *)b)->x; }
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static int best_rect(Pt *p, int n, int w, int h, int *bx, int *bz) {
    int best = 0;
    int *zs = (int *)malloc(sizeof(int) * (n ? n : 1));
    qsort(p, n, sizeof(Pt), cmp_x);
    int hi = 0;
    for (int lo = 0; lo < n; lo++) {
        if (lo > 0 && p[lo].x == p[lo - 1].x) continue;
        int x0 = p[lo].x;
        while (hi < n && p[hi].x <= x0 + w - 1) hi++;
        int m = 0;
        for (int i = lo; i < hi; i++) zs[m++] = p[i].z;
        qsort(zs, m, sizeof(int), cmp_int);
        int top = 0;
        for (int a = 0; a < m; a++) {
            while (top < m && zs[top] <= zs[a] + h - 1) top++;
            if (top - a > best) { best = top - a; *bx = x0; *bz = zs[a]; }
        }
    }
    free(zs);
    return best;
}

static int count_circle(const Pt *p, int n, int cx, int cz, int64_t r2) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        int64_t dx = p[i].x - cx, dz = p[i].z - cz;
        if (dx * dx + dz * dz <= r2) c++;
    }
    return c;
}

typedef struct { double key; int d; double cx, cz; } Ev;

static int cmp_ev(const void *a, const void *b) {
    const Ev *x = (const Ev *)a, *y = (const Ev *)b;
    if (x->key < y->key) return -1;
    if (x->key > y->key) return 1;
    if (x->d != y->d) return y->d - x->d;
    if (x->cx < y->cx) return -1;
    if (x->cx > y->cx) return 1;
    if (x->cz < y->cz) return -1;
    if (x->cz > y->cz) return 1;
    return 0;
}

static double pseudo_angle(double x, double z) {
    if (z >= 0) return x >= 0 ? (x + z == 0 ? 0.0 : z / (x + z)) : 1.0 - x / (-x + z);
    return x < 0 ? 2.0 - z / (-x - z) : 3.0 + x / (x - z);
}

static int best_circle(const Pt *p, int n, int r, int *bx, int *bz) {
    int64_t r2 = (int64_t)r * r;
    double R = r;
    int best = 0;
    Ev *ev = (Ev *)malloc(sizeof(Ev) * ((size_t)2 * n + 2));
    if (!ev) return 0;
    for (int i = 0; i < n; i++) {
        int ne = 0, base = 1;
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            double dx = p[j].x - p[i].x, dz = p[j].z - p[i].z;
            double d2 = dx * dx + dz * dz;
            if (d2 == 0) { base++; continue; }
            if (d2 > 4.0 * R * R) continue;
            double d = sqrt(d2);
            double h = sqrt(R * R - d2 / 4.0);
            double mx = p[i].x + dx / 2.0, mz = p[i].z + dz / 2.0;
            double ux = -dz / d * h, uz = dx / d * h;
            double ex = mx - ux, ez = mz - uz, lx = mx + ux, lz = mz + uz;
            double ke = pseudo_angle(ex - p[i].x, ez - p[i].z), kl = pseudo_angle(lx - p[i].x, lz - p[i].z);
            if (ke > kl) base++;
            ev[ne].key = ke; ev[ne].d = +1; ev[ne].cx = ex; ev[ne].cz = ez; ne++;
            ev[ne].key = kl; ev[ne].d = -1; ev[ne].cx = lx; ev[ne].cz = lz; ne++;
        }
        qsort(ev, ne, sizeof(Ev), cmp_ev);
        int cur = base, local = base;
        double atx = p[i].x + R, atz = p[i].z;
        for (int k = 0; k < ne; k++) {
            cur += ev[k].d;
            if (cur > local) { local = cur; atx = ev[k].cx; atz = ev[k].cz; }
        }
        int ix = (int)floor(atx), iz = (int)floor(atz);
        for (int ox = -1; ox <= 2; ox++) {
            for (int oz = -1; oz <= 2; oz++) {
                int c = count_circle(p, n, ix + ox, iz + oz, r2);
                if (c > best) { best = c; *bx = ix + ox; *bz = iz + oz; }
            }
        }
        int c = count_circle(p, n, p[i].x, p[i].z, r2);
        if (c > best) { best = c; *bx = p[i].x; *bz = p[i].z; }
    }
    free(ev);
    return best;
}

const char *const MOB_NAMES[MOB_COUNT] = {"ZOMBIE", "HUSK", "SPIDER", "SLIME", "CAVE_SPIDER", "SILVERFISH",
                                          "BABY_ZOMBIE", "SKELETON", "STRAY", "BOGGED", "BREEZE"};
static const char *const MOB_WORDS[MOB_COUNT] = {"zombie", "husk", "spider", "slime", "cave_spider", "silverfish",
                                                 "baby_zombie", "skeleton", "stray", "bogged", "breeze"};
static const char *const MOB_TEMPLATES[MOB_COUNT] = {"zombie", "husk", "spider", "slime", "cave_spider", "silverfish",
                                                     "baby_zombie", "skeleton", "stray", "poison_skeleton", "breeze"};

static int mob_of_template(int tpl) {
    const char *name = tc_template_name(tpl);
    const char *last = strrchr(name, '/');
    last = last ? last + 1 : name;
    for (int i = 0; i < MOB_COUNT; i++) {
        if (strcmp(last, MOB_TEMPLATES[i]) == 0) return i;
    }
    return -1;
}

static int floor_shift(int v, int s) { return v >= 0 ? v >> s : ~((~v) >> s); }

static int region_of(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static int cmp_result(const void *a, const void *b) {
    const Result *x = (const Result *)a, *y = (const Result *)b;
    if (SORT_MODE == SORT_CLOSENESS && x->span != y->span) return x->span < y->span ? -1 : 1;
    if (x->count != y->count) return y->count - x->count;
    if (x->hash != y->hash) return x->hash < y->hash ? -1 : 1;
    if (x->arx != y->arx) return x->arx - y->arx;
    return x->arz - y->arz;
}

static int cmp_display(const void *a, const void *b) {
    const Result *x = (const Result *)a, *y = (const Result *)b;
    if (SORT_BY_SPAN && x->span != y->span) return x->span < y->span ? -1 : 1;
    if (x->count != y->count) return y->count - x->count;
    if (x->arx != y->arx) return x->arx - y->arx;
    return x->arz - y->arz;
}

static unsigned hash_region(unsigned h, int rx, int rz) {
    h = (h ^ (unsigned)rx) * 16777619u;
    return (h ^ (unsigned)rz) * 16777619u;
}

static int wanted(const InputData *in, int kind, int mob) {
    switch (in->count_kind) {
    case COUNT_SPAWNERS: return kind == KIND_SPAWNER && !(mob >= 0 && in->exclude[mob]);
    case COUNT_VAULTS: return kind == KIND_VAULT || kind == KIND_OMINOUS_VAULT;
    default: return kind == KIND_OMINOUS_VAULT;
    }
}

static int inside(const Area *a, const Result *r, int x, int z) {
    if (a->shape == SHAPE_CIRCLE) {
        int64_t dx = x - r->x, dz = z - r->z;
        return dx * dx + dz * dz <= (int64_t)a->radius * a->radius;
    }
    return x >= r->minX && x <= r->minX + r->shape_w - 1 && z >= r->minZ && z <= r->minZ + r->shape_h - 1;
}

static struct { double total, done, tile; int rows, last_pct; volatile long rows_done; time_t t0; } PROG;

static void prog_report(long rows) {
    if (PROG.total <= 0) return;
    double part = PROG.rows > 0 ? (double)rows / PROG.rows : 0;
    int pct = (int)((PROG.done + PROG.tile * part) * 100 / PROG.total);
    if (pct > 99) pct = 99;
    if (pct / 10 <= PROG.last_pct / 10) return;
    PROG.last_pct = pct;
    printf("  %d%% (%.0fs)\n", pct, difftime(time(NULL), PROG.t0));
    fflush(stdout);
}

static double tile_weight(const InputData *in, int ci0, int cj0, int ci1, int cj1) {
    int64_t r2 = (int64_t)in->search_radius * in->search_radius;
    int inside_n = 0;
    for (int a = 0; a < 16; a++) {
        for (int b = 0; b < 16; b++) {
            int64_t x = (int64_t)(ci0 + (ci1 - ci0) * a / 15) * REGION_BLOCKS + REGION_BLOCKS / 2 - in->center_x;
            int64_t z = (int64_t)(cj0 + (cj1 - cj0) * b / 15) * REGION_BLOCKS + REGION_BLOCKS / 2 - in->center_z;
            if (x * x + z * z <= r2 + (int64_t)2 * REGION_BLOCKS * REGION_BLOCKS) inside_n++;
        }
    }
    return (double)(ci1 - ci0 + 1) * (cj1 - cj0 + 1) * inside_n / 256.0;
}

static void out_of_memory(void) {
    printf("Not enough memory for this search. Try a smaller area or fewer threads.\n");
    exit(1);
}

typedef struct {
    Ch *chs; int nch, cap;
    Off *pts; int npts, pcap;
    int n_deep;
} Row;

typedef struct {
    Ch *chs;
    int nch;
    int *grid;
    int side, rx0, rz0;
    int ci0, cj0, ci1, cj1;
    Row *rows;
    int n_deep;
} Scan;

typedef struct {
    const InputData *in;
    Scan *s;
    volatile long next_row, rows_done;
} ScanJob;

static void scan_rows(void *arg, int worker) {
    ScanJob *job = (ScanJob *)arg;
    const InputData *in = job->in;
    Scan *s = job->s;
    TcChamber *c = (TcChamber *)malloc(sizeof(TcChamber));
    TcWork *tw = tc_work_new();
    BiomeNoise *bn = (BiomeNoise *)malloc(sizeof(BiomeNoise));
    if (!c || !tw || !bn) out_of_memory();
    initBiomeNoise(bn, MC_1_21_1);
    setBiomeSeed(bn, (uint64_t)in->seed, 0);
    int64_t sr2 = (int64_t)in->search_radius * in->search_radius;
    int deep_ok = tc_deep_dark_allowed();
    for (;;) {
        long i = tc_atomic_next(&job->next_row);
        if (i >= s->side) break;
        Row *row = &s->rows[i];
        memset(row, 0, sizeof *row);
        for (int j = 0; j < s->side; j++) {
            int rx = s->rx0 + (int)i, rz = s->rz0 + j;
            int chunkX, chunkZ, px, py, pz;
            tc_candidate(in->seed, rx, rz, &chunkX, &chunkZ);
            tc_start_pos(in->seed, chunkX, chunkZ, &px, &py, &pz);
            int64_t dx = px - in->center_x, dz = pz - in->center_z;
            if (dx * dx + dz * dz > sr2) continue;

            if (!deep_ok && sampleBiomeNoise(bn, NULL, floor_shift(px, 2), floor_shift(py, 2), floor_shift(pz, 2), NULL, 0) == deep_dark) {
                if (i >= s->ci0 && i <= s->ci1 && j >= s->cj0 && j <= s->cj1) row->n_deep++;
                continue;
            }
            if (tc_generate_w(tw, c, in->seed, chunkX, chunkZ) != 0) continue;
            if (row->nch == row->cap) {
                row->cap = row->cap ? row->cap * 2 : 16;
                row->chs = (Ch *)realloc(row->chs, sizeof(Ch) * row->cap);
                if (!row->chs) out_of_memory();
            }
            if (row->npts + c->nblocks > row->pcap) {
                while (row->npts + c->nblocks > row->pcap) row->pcap = row->pcap ? row->pcap * 2 : 1024;
                row->pts = (Off *)realloc(row->pts, sizeof(Off) * row->pcap);
                if (!row->pts) out_of_memory();
            }
            Ch *h = &row->chs[row->nch];
            h->rx = rx; h->rz = rz;
            h->posX = c->posX; h->posY = c->posY; h->posZ = c->posZ;
            h->pts = NULL;
            int n0 = row->npts;
            for (int b = 0; b < c->nblocks; b++) {
                const TcBlock *bl = &c->blocks[b];
                int mob = bl->kind == KIND_SPAWNER ? MOB_OF[bl->tpl] : -1;
                if (wanted(in, bl->kind, mob)) row->pts[row->npts++] = (Off){(signed char)(bl->x - c->posX), (signed char)(bl->z - c->posZ)};
            }
            h->npts = row->npts - n0;
            row->nch++;
        }
        if (row->npts) {
            Off *p = (Off *)realloc(row->pts, sizeof(Off) * row->npts);
            if (p) row->pts = p;
        }
        long rows = tc_atomic_next(&PROG.rows_done) + 1;
        if (worker == 0) prog_report(rows);
    }
    free(bn);
    tc_work_free(tw);
    free(c);
}

static void scan_window(const InputData *in, Scan *s, int rx0, int rz0, int side, int ci0, int cj0, int ci1, int cj1) {
    s->rx0 = rx0;
    s->rz0 = rz0;
    s->side = side;
    s->ci0 = ci0; s->cj0 = cj0; s->ci1 = ci1; s->cj1 = cj1;
    PROG.rows = side;
    int64_t nreg = (int64_t)side * side;
    ScanJob job;
    memset(&job, 0, sizeof job);
    job.in = in;
    job.s = s;
    s->rows = (Row *)calloc((size_t)side, sizeof(Row));
    if (!s->rows) out_of_memory();
    tc_parallel(in->threads, scan_rows, &job);

    int total_ch = 0;
    s->n_deep = 0;
    for (int i = 0; i < side; i++) { total_ch += s->rows[i].nch; s->n_deep += s->rows[i].n_deep; }
    s->chs = (Ch *)malloc(sizeof(Ch) * (total_ch ? total_ch : 1));
    s->grid = (int *)malloc(sizeof(int) * (size_t)nreg);
    if (!s->chs || !s->grid) out_of_memory();
    for (int64_t k = 0; k < nreg; k++) s->grid[k] = -1;
    s->nch = 0;
    for (int i = 0; i < side; i++) {
        Row *row = &s->rows[i];
        int off = 0;
        for (int k = 0; k < row->nch; k++) {
            Ch h = row->chs[k];
            h.pts = row->pts + off;
            off += h.npts;
            s->chs[s->nch] = h;
            s->grid[(int64_t)(h.rx - rx0) * side + (h.rz - rz0)] = s->nch++;
        }
        free(row->chs);
        row->chs = NULL;
    }
}

static void scan_free(Scan *s) {
    for (int i = 0; i < s->side; i++) free(s->rows[i].pts);
    free(s->rows); free(s->grid); free(s->chs);
}

typedef struct { void *p; int n, cap; } Buf;

static void *buf_room(Buf *b, int more, size_t size) {
    if (b->n + more > b->cap) {
        while (b->n + more > b->cap) b->cap = b->cap ? b->cap * 2 : 1024;
        void *p = realloc(b->p, size * (size_t)b->cap);
        if (!p) out_of_memory();
        b->p = p;
    }
    return (char *)b->p + size * (size_t)b->n;
}

static int neighbours(const Scan *s, int a, int reach, Buf *out) {
    const Ch *A = &s->chs[a];
    int ai = A->rx - s->rx0, aj = A->rz - s->rz0;
    int nb = reach / REGION_BLOCKS + 1;
    out->n = 0;
    for (int di = -nb; di <= nb; di++) {
        for (int dj = -nb; dj <= nb; dj++) {
            int i = ai + di, j = aj + dj;
            if (i < 0 || j < 0 || i >= s->side || j >= s->side) continue;
            int k = s->grid[i * s->side + j];
            if (k < 0) continue;
            const Ch *B = &s->chs[k];
            int64_t dx = B->posX - A->posX, dz = B->posZ - A->posZ;
            if (dx * dx + dz * dz > (int64_t)reach * reach) continue;
            *(int *)buf_room(out, 1, sizeof(int)) = k;
            out->n++;
        }
    }
    return out->n;
}

static int gather(const Scan *s, int a, int reach, Buf *nb, Buf *out) {
    neighbours(s, a, reach, nb);
    out->n = 0;
    for (int i = 0; i < nb->n; i++) {
        int k = ((int *)nb->p)[i];
        const Ch *B = &s->chs[k];
        Pt *p = (Pt *)buf_room(out, B->npts, sizeof(Pt));
        for (int q = 0; q < B->npts; q++) p[q] = (Pt){B->posX + B->pts[q].dx, B->posZ + B->pts[q].dz, k};
        out->n += B->npts;
    }
    return out->n;
}

static int chamber_at(const InputData *in, int rx, int rz, TcWork *tw, TcChamber *c, BiomeNoise *bn, int deep_ok) {
    int chunkX, chunkZ, px, py, pz;
    tc_candidate(in->seed, rx, rz, &chunkX, &chunkZ);
    tc_start_pos(in->seed, chunkX, chunkZ, &px, &py, &pz);
    int64_t dx = px - in->center_x, dz = pz - in->center_z;
    if (dx * dx + dz * dz > (int64_t)in->search_radius * in->search_radius) return 0;
    if (!deep_ok && sampleBiomeNoise(bn, NULL, floor_shift(px, 2), floor_shift(py, 2), floor_shift(pz, 2), NULL, 0) == deep_dark) return 0;
    return tc_generate_w(tw, c, in->seed, chunkX, chunkZ) == 0;
}

static void add_blocks(const TcChamber *c, Buf *out) {
    Block *b = (Block *)buf_room(out, c->nblocks, sizeof(Block));
    for (int i = 0; i < c->nblocks; i++) {
        const TcBlock *bl = &c->blocks[i];
        b[i] = (Block){bl->x, bl->z, (short)bl->kind, (short)(bl->kind == KIND_SPAWNER ? MOB_OF[bl->tpl] : -1)};
    }
    out->n += c->nblocks;
}

static void blocks_around(const InputData *in, const Area *ar, const Result *r, Buf *out) {
    TcChamber *c = (TcChamber *)malloc(sizeof(TcChamber));
    TcWork *tw = tc_work_new();
    BiomeNoise *bn = (BiomeNoise *)malloc(sizeof(BiomeNoise));
    if (!c || !tw || !bn) out_of_memory();
    initBiomeNoise(bn, MC_1_21_1);
    setBiomeSeed(bn, (uint64_t)in->seed, 0);
    int deep_ok = tc_deep_dark_allowed();
    int half = ar->shape == SHAPE_CIRCLE ? ar->radius
                                         : (int)(sqrt((double)r->shape_w * r->shape_w + (double)r->shape_h * r->shape_h) / 2) + 1;
    int want = half + CHAMBER_REACH;
    int crx = region_of(r->x, REGION_BLOCKS), crz = region_of(r->z, REGION_BLOCKS);
    int nb = want / REGION_BLOCKS + 1;
    out->n = 0;
    for (int di = -nb; di <= nb; di++) {
        for (int dj = -nb; dj <= nb; dj++) {
            if (!chamber_at(in, crx + di, crz + dj, tw, c, bn, deep_ok)) continue;
            int64_t dx = c->posX - r->x, dz = c->posZ - r->z;
            if (dx * dx + dz * dz > (int64_t)want * want) continue;
            add_blocks(c, out);
        }
    }
    free(bn);
    tc_work_free(tw);
    free(c);
}

static void print_breakdown(const InputData *in, const Block *all, int n, const Area *a, const Result *r) {
    int mobs[MOB_COUNT] = {0}, spawners = 0, vaults = 0, ominous = 0;
    for (int i = 0; i < n; i++) {
        const Block *p = &all[i];
        if (a && !inside(a, r, p->x, p->z)) continue;
        if (p->kind == KIND_SPAWNER) { spawners++; if (p->mob >= 0) mobs[p->mob]++; }
        else if (p->kind == KIND_VAULT) vaults++;
        else if (p->kind == KIND_OMINOUS_VAULT) { vaults++; ominous++; }
    }
    printf("    spawners %d (", spawners);
    int first = 1;
    for (int m = 0; m < MOB_COUNT; m++) {
        if (!mobs[m]) continue;
        printf("%s%d %s%s", first ? "" : ", ", mobs[m], MOB_WORDS[m], in->exclude[m] ? " [left out]" : "");
        first = 0;
    }
    printf(")  vaults %d (ominous %d)\n", vaults, ominous);
}

static int keep_best(Result *res, int n, int64_t show) {
    if (n == 0) return 0;
    qsort(res, n, sizeof(Result), cmp_result);
    int m = 0;
    for (int i = 0; i < n && m < show; i++) {
        if (m > 0 && res[i].hash == res[m - 1].hash && res[i].count == res[m - 1].count) continue;
        res[m++] = res[i];
    }
    return m;
}

typedef struct {
    const InputData *in;
    const Scan *s;
    const Area *ar;
    int reach;
    int ci0, ci1, cj0, cj1;
    long nchunks;
    volatile long next;
    Buf *res;
} AreaJob;

static int in_core(const AreaJob *job, const Ch *h) {
    int i = h->rx - job->s->rx0, j = h->rz - job->s->rz0;
    return i >= job->ci0 && i <= job->ci1 && j >= job->cj0 && j <= job->cj1;
}

static int area_result(const AreaJob *job, int a, Buf *nb, Buf *pts, Buf *seen, Result *r) {
    const InputData *in = job->in;
    const Area *ar = job->ar;
    const Scan *s = job->s;
    int m = gather(s, a, job->reach, nb, pts);
    if (m < in->min_count) return 0;
    Pt *local = (Pt *)pts->p;
    memset(r, 0, sizeof *r);
    r->arx = s->chs[a].rx;
    r->arz = s->chs[a].rz;
    if (ar->shape == SHAPE_CIRCLE) {
        r->count = best_circle(local, m, ar->radius, &r->x, &r->z);
    } else {
        int bx = 0, bz = 0, bx2 = 0, bz2 = 0;
        r->shape_w = ar->side_w; r->shape_h = ar->side_h;
        r->count = best_rect(local, m, ar->side_w, ar->side_h, &bx, &bz);
        if (ar->shape == SHAPE_RECTANGLE && ar->side_w != ar->side_h) {
            int c2 = best_rect(local, m, ar->side_h, ar->side_w, &bx2, &bz2);
            if (c2 > r->count) { r->count = c2; bx = bx2; bz = bz2; r->shape_w = ar->side_h; r->shape_h = ar->side_w; }
        }
        r->minX = bx; r->minZ = bz;
        r->x = bx + (r->shape_w - 1) / 2; r->z = bz + (r->shape_h - 1) / 2;
    }
    if (r->count < in->min_count) return 0;

    seen->n = 0;
    int *ids = (int *)buf_room(seen, m, sizeof(int));
    int nin = 0, ns = 0;
    for (int q = 0; q < m; q++) if (inside(ar, r, local[q].x, local[q].z)) ids[nin++] = local[q].ch;
    qsort(ids, nin, sizeof(int), cmp_int);
    unsigned h = 2166136261u;
    for (int t = 0; t < nin; t++) {
        if (t > 0 && ids[t] == ids[t - 1]) continue;
        h = hash_region(h, s->chs[ids[t]].rx, s->chs[ids[t]].rz);
        ns++;
    }
    r->hash = h;
    r->nchambers = ns;
    return 1;
}

static int nearest(const Scan *s, int a, int k, int *out, int64_t *d) {
    const Ch *A = &s->chs[a];
    int ai = A->rx - s->rx0, aj = A->rz - s->rz0, n = 0;
    int slack = REGION_BLOCKS + 2 * CHAMBER_REACH;
    for (int r = 1; r <= s->side; r++) {
        int64_t lb = (int64_t)r * REGION_BLOCKS - slack;
        if (n == k && lb > 0 && lb * lb > d[k - 1]) break;
        for (int di = -r; di <= r; di++) {
            int step = di == -r || di == r ? 1 : 2 * r;
            for (int dj = -r; dj <= r; dj += step) {
                int i = ai + di, j = aj + dj;
                if (i < 0 || j < 0 || i >= s->side || j >= s->side) continue;
                int c = s->grid[i * s->side + j];
                if (c < 0) continue;
                int64_t dx = s->chs[c].posX - A->posX, dz = s->chs[c].posZ - A->posZ, dd = dx * dx + dz * dz;
                if (n == k && (dd > d[k - 1] || (dd == d[k - 1] && c > out[k - 1]))) continue;
                int p = n < k ? n++ : k - 1;
                while (p > 0 && (d[p - 1] > dd || (d[p - 1] == dd && out[p - 1] > c))) { d[p] = d[p - 1]; out[p] = out[p - 1]; p--; }
                d[p] = dd; out[p] = c;
            }
        }
    }
    return n;
}

static int group_result(const AreaJob *job, int a, Result *r) {
    const Scan *s = job->s;
    int n = job->ar->chambers, mem[4], ids[4];
    int64_t d[4];
    mem[0] = a;
    if (n > 1 && nearest(s, a, n - 1, mem + 1, d) != n - 1) return 0;
    memset(r, 0, sizeof *r);
    r->arx = s->chs[a].rx;
    r->arz = s->chs[a].rz;
    r->nchambers = n;
    for (int i = 0; i < n; i++) { r->count += s->chs[mem[i]].npts; ids[i] = mem[i]; }
    if (r->count < job->in->min_count) return 0;
    for (int i = 0; i < n; i++) { r->mem[i][0] = s->chs[mem[i]].rx; r->mem[i][1] = s->chs[mem[i]].rz; }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int64_t dx = s->chs[mem[i]].posX - s->chs[mem[j]].posX, dz = s->chs[mem[i]].posZ - s->chs[mem[j]].posZ;
            int64_t dd = dx * dx + dz * dz;
            if (dd > r->span) r->span = dd;
        }
    }
    qsort(ids, n, sizeof(int), cmp_int);
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) h = hash_region(h, s->chs[ids[i]].rx, s->chs[ids[i]].rz);
    r->hash = h;
    return 1;
}

static void area_worker(void *arg, int worker) {
    AreaJob *job = (AreaJob *)arg;
    const Scan *s = job->s;
    Buf nb = {0}, pts = {0}, seen = {0};
    Buf *out = &job->res[worker];
    int64_t limit = job->in->show * 2 + 4096;
    for (;;) {
        long k = tc_atomic_next(&job->next);
        if (k >= job->nchunks) break;
        int a0 = (int)(k * ANCHOR_CHUNK), a1 = a0 + ANCHOR_CHUNK < s->nch ? a0 + ANCHOR_CHUNK : s->nch;
        for (int a = a0; a < a1; a++) {
            if (!in_core(job, &s->chs[a])) continue;
            Result r;
            int ok = job->ar->shape == SHAPE_CHAMBERS ? group_result(job, a, &r) : area_result(job, a, &nb, &pts, &seen, &r);
            if (!ok) continue;
            *(Result *)buf_room(out, 1, sizeof(Result)) = r;
            if (++out->n > limit) out->n = keep_best((Result *)out->p, out->n, job->in->show);
        }
    }
    free(nb.p); free(pts.p); free(seen.p);
}

static void run_tile(const InputData *in, const Area *ar, int reach, int halo, int ci0, int cj0, int ci1, int cj1,
                     Buf *all, int *nch, int *n_deep) {
    Scan s;
    memset(&s, 0, sizeof s);
    int w = (ci1 - ci0 + 1) + 2 * halo, h = (cj1 - cj0 + 1) + 2 * halo;
    scan_window(in, &s, ci0 - halo, cj0 - halo, w > h ? w : h, halo, halo, halo + (ci1 - ci0), halo + (cj1 - cj0));
    AreaJob job;
    memset(&job, 0, sizeof job);
    job.in = in; job.s = &s; job.ar = ar; job.reach = reach;
    job.ci0 = s.ci0; job.cj0 = s.cj0; job.ci1 = s.ci1; job.cj1 = s.cj1;
    job.nchunks = (s.nch + ANCHOR_CHUNK - 1) / ANCHOR_CHUNK;
    job.res = (Buf *)calloc((size_t)in->threads, sizeof(Buf));
    if (!job.res) out_of_memory();
    tc_parallel(in->threads, area_worker, &job);
    for (int t = 0; t < in->threads; t++) {
        Buf *b = &job.res[t];
        if (b->n) memcpy(buf_room(all, b->n, sizeof(Result)), b->p, sizeof(Result) * b->n);
        all->n += b->n;
        free(b->p);
    }
    free(job.res);
    for (int i = 0; i < s.nch; i++) if (in_core(&job, &s.chs[i])) (*nch)++;
    *n_deep += s.n_deep;
    scan_free(&s);
}

static void print_results(const InputData *in, const Area *ar, const Result *res, int nres) {
    TcChamber *c = (TcChamber *)malloc(sizeof(TcChamber));
    TcWork *tw = tc_work_new();
    BiomeNoise *bn = (BiomeNoise *)malloc(sizeof(BiomeNoise));
    if (!c || !tw || !bn) out_of_memory();
    initBiomeNoise(bn, MC_1_21_1);
    setBiomeSeed(bn, (uint64_t)in->seed, 0);
    int deep_ok = tc_deep_dark_allowed();
    Buf blocks = {0};
    for (int i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (ar->shape == SHAPE_CHAMBERS) {
            int n = r->nchambers;
            int px[4] = {0}, py[4] = {0}, pz[4] = {0}, cnt[4] = {0};
            blocks.n = 0;
            for (int p = 0; p < n; p++) {
                if (!chamber_at(in, r->mem[p][0], r->mem[p][1], tw, c, bn, deep_ok)) continue;
                px[p] = c->posX; py[p] = c->posY; pz[p] = c->posZ;
                cnt[p] = 0;
                for (int b = 0; b < c->nblocks; b++) {
                    const TcBlock *bl = &c->blocks[b];
                    cnt[p] += wanted(in, bl->kind, bl->kind == KIND_SPAWNER ? MOB_OF[bl->tpl] : -1);
                }
                add_blocks(c, &blocks);
            }
            if (n == 1) {
                printf("Found %d %s in 1 chamber at /tp %d %d %d\n", r->count, COUNT_WORDS[in->count_kind], px[0], py[0], pz[0]);
            } else {
                int64_t far = 0;
                for (int p = 0; p < n; p++) {
                    for (int q = p + 1; q < n; q++) {
                        int64_t dx = px[p] - px[q], dz = pz[p] - pz[q];
                        if (dx * dx + dz * dz > far) far = dx * dx + dz * dz;
                    }
                }
                printf("Found %d %s in %d chambers, up to %.0f blocks apart\n", r->count, COUNT_WORDS[in->count_kind], n, sqrt((double)far));
                for (int p = 0; p < n; p++)
                    printf("    /tp %d %d %d  (%d %s)\n", px[p], py[p], pz[p], cnt[p], COUNT_WORDS[in->count_kind]);
            }
            print_breakdown(in, (const Block *)blocks.p, blocks.n, NULL, NULL);
            continue;
        }
        char where[160];
        if (ar->shape == SHAPE_CIRCLE)
            snprintf(where, sizeof where, "circle r=%d", ar->radius);
        else if (ar->shape == SHAPE_SQUARE)
            snprintf(where, sizeof where, "square r=%d (%d %d to %d %d)", ar->radius, r->minX, r->minZ,
                     r->minX + r->shape_w - 1, r->minZ + r->shape_h - 1);
        else
            snprintf(where, sizeof where, "%dx%d rectangle (%d %d to %d %d)", r->shape_w, r->shape_h, r->minX, r->minZ,
                     r->minX + r->shape_w - 1, r->minZ + r->shape_h - 1);
        printf("Found %d %s (%d chamber%s) in %s at /tp %d ~ %d\n", r->count, COUNT_WORDS[in->count_kind],
               r->nchambers, r->nchambers == 1 ? "" : "s", where, r->x, r->z);
        blocks_around(in, ar, r, &blocks);
        print_breakdown(in, (const Block *)blocks.p, blocks.n, ar, r);
    }
    free(blocks.p);
    free(bn);
    tc_work_free(tw);
    free(c);
}

int search_run(const InputData *in) {
    tc_select_version(in->version);
    REGION_BLOCKS = tc_region_blocks();
    for (int t = 0; t < TC_NUM_TEMPLATES_MAX; t++) MOB_OF[t] = t < tc_num_templates() ? mob_of_template(t) : -1;
    const Area *ar = &in->area;
    SORT_MODE = ar->shape == SHAPE_CHAMBERS && ar->chambers > 1 ? in->sort : SORT_AMOUNT;
    SORT_BY_SPAN = SORT_MODE != SORT_AMOUNT;
    int extent = ar->shape == SHAPE_CIRCLE ? 2 * ar->radius + 1 : (ar->side_w > ar->side_h ? ar->side_w : ar->side_h);
    int reach = ar->shape == SHAPE_CHAMBERS ? 0 : extent + 2 * CHAMBER_REACH;
    int halo = ar->shape == SHAPE_CHAMBERS ? GROUP_HALO : reach / REGION_BLOCKS + 2;

    int rr = in->search_radius / REGION_BLOCKS + 1;
    int rcx = region_of(in->center_x, REGION_BLOCKS), rcz = region_of(in->center_z, REGION_BLOCKS);
    int i0 = rcx - rr, i1 = rcx + rr, j0 = rcz - rr, j1 = rcz + rr;
    int64_t bytes_per_region = 80;
    int tile = (int)(sqrt((double)TILE_BYTES / bytes_per_region)) - 2 * halo;
    if (tile < 4 * halo) tile = 4 * halo;
    if (tile < TILE_MIN) tile = TILE_MIN;
    if (tile > 2 * rr + 1) tile = 2 * rr + 1;

    printf("Scanning %" PRId64 " regions on %d thread%s...\n", (int64_t)(2 * rr + 1) * (2 * rr + 1), in->threads,
           in->threads == 1 ? "" : "s");
    Buf all = {0};
    int nch = 0, n_deep = 0;
    time_t t0 = time(NULL);
    PROG.total = 0;
    for (int i = i0; i <= i1; i += tile) {
        for (int j = j0; j <= j1; j += tile) {
            int ci1 = i + tile - 1 < i1 ? i + tile - 1 : i1, cj1 = j + tile - 1 < j1 ? j + tile - 1 : j1;
            PROG.total += tile_weight(in, i, j, ci1, cj1);
        }
    }
    PROG.done = 0;
    PROG.last_pct = 0;
    PROG.t0 = t0;
    for (int i = i0; i <= i1; i += tile) {
        for (int j = j0; j <= j1; j += tile) {
            int ci1 = i + tile - 1 < i1 ? i + tile - 1 : i1, cj1 = j + tile - 1 < j1 ? j + tile - 1 : j1;
            PROG.tile = tile_weight(in, i, j, ci1, cj1);
            PROG.rows_done = 0;
            run_tile(in, ar, reach, halo, i, j, ci1, cj1, &all, &nch, &n_deep);
            PROG.done += PROG.tile;
            all.n = keep_best((Result *)all.p, all.n, in->show);
        }
    }
    printf("%d chambers in range (%d skipped: deep dark), %.0fs\n\n", nch, n_deep, difftime(time(NULL), t0));
    int nres = keep_best((Result *)all.p, all.n, in->show);
    qsort(all.p, nres, sizeof(Result), cmp_display);
    print_results(in, ar, (const Result *)all.p, nres);
    if (nres == 0) printf("Nothing found with at least %" PRId64 " %s\n", in->min_count, COUNT_WORDS[in->count_kind]);
    free(all.p);
    return 0;
}
