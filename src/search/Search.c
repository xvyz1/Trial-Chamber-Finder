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
#ifndef TILE_MIN
#define TILE_MIN 32
#endif

const char *const COUNT_NAMES[COUNT_KINDS] = {"TRIAL_SPAWNERS", "VAULTS", "OMINOUS_VAULTS"};
static const char *const COUNT_WORDS[COUNT_KINDS] = {"trial spawners", "vaults", "ominous vaults"};
const char *const SHAPE_NAMES[SHAPE_COUNT] = {"CHAMBERS", "SQUARE", "RECTANGLE"};
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

#define RECT_BIG (1 << 24)

typedef struct { int *mx, *add, size; } RectTree;

static void rt_update(RectTree *t, int node, int l, int r, int ql, int qr, int v) {
    if (qr < l || r < ql) return;
    if (ql <= l && r <= qr) { t->mx[node] += v; t->add[node] += v; return; }
    int m = (l + r) >> 1;
    rt_update(t, node * 2, l, m, ql, qr, v);
    rt_update(t, node * 2 + 1, m + 1, r, ql, qr, v);
    int a = t->mx[node * 2], b = t->mx[node * 2 + 1];
    t->mx[node] = (a > b ? a : b) + t->add[node];
}

static int best_rect(Pt *pts, int n, int w, int h, int *bx, int *bz) {
    int *p = (int *)pts;
    if (n == 0) return 0;
    qsort(p, n, 3 * sizeof(int), cmp_x);
    int *zs = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) zs[i] = p[3 * i + 1];
    qsort(zs, n, sizeof(int), cmp_int);
    int nz = 0;
    for (int i = 0; i < n; i++) if (i == 0 || zs[i] != zs[i - 1]) zs[nz++] = zs[i];
    int *zi = (int *)malloc(sizeof(int) * n), *lo_idx = (int *)malloc(sizeof(int) * n), *mult = (int *)calloc((size_t)nz, sizeof(int));
    RectTree t; t.size = nz; t.mx = (int *)calloc((size_t)nz * 4, sizeof(int)); t.add = (int *)calloc((size_t)nz * 4, sizeof(int));
    for (int i = 0; i < n; i++) {
        int z = p[3 * i + 1], a = 0, b = nz;
        while (a < b) { int m = (a + b) >> 1; if (zs[m] < z) a = m + 1; else b = m; }
        zi[i] = a;
        int target = z - h + 1; a = 0; b = nz;
        while (a < b) { int m = (a + b) >> 1; if (zs[m] < target) a = m + 1; else b = m; }
        lo_idx[i] = a;
    }
    int best = 0, rem = 0, hi = 0;
    for (int lo = 0; lo < n; lo++) {
        if (lo > 0 && p[3 * lo] == p[3 * (lo - 1)]) continue;
        int x0 = p[3 * lo];
        while (rem < lo) {
            int q = rem++;
            rt_update(&t, 1, 0, nz - 1, lo_idx[q], zi[q], -1);
            if (--mult[zi[q]] == 0) rt_update(&t, 1, 0, nz - 1, zi[q], zi[q], -RECT_BIG);
        }
        while (hi < n && p[3 * hi] <= x0 + w - 1) {
            int q = hi++;
            rt_update(&t, 1, 0, nz - 1, lo_idx[q], zi[q], +1);
            if (mult[zi[q]]++ == 0) rt_update(&t, 1, 0, nz - 1, zi[q], zi[q], +RECT_BIG);
        }
        int cnt = t.mx[1] - RECT_BIG;
        if (cnt > best) {
            int node = 1, l = 0, r = nz - 1;
            while (l < r) { int m = (l + r) >> 1; if (t.mx[node * 2] >= t.mx[node * 2 + 1]) { node = node * 2; r = m; } else { node = node * 2 + 1; l = m + 1; } }
            best = cnt; *bx = x0; *bz = zs[l];
        }
    }
    free(zs); free(zi); free(lo_idx); free(mult); free(t.mx); free(t.add);
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
    (void)a;
    return x >= r->minX && x <= r->minX + r->shape_w - 1 && z >= r->minZ && z <= r->minZ + r->shape_h - 1;
}

#define PROG_QUIET_SECONDS 10.0
static struct {
    double total, done, tile, t0, quiet;
    double scan_share, scan_time, area_time;
    double *rowcum;
    int rows, phase, last_pct, shown;
    volatile long rows_done;
} PROG;

static void fmt_left(double s, char *out, size_t n) {
    long v = (long)(s + 0.5);
    if (v < 0) v = 0;
    if (v >= 3600) snprintf(out, n, "%ld h %02ld min", v / 3600, (v % 3600) / 60);
    else if (v >= 60) snprintf(out, n, "%ld min %02ld s", v / 60, v % 60);
    else snprintf(out, n, "%ld s", v);
}

static void prog_emit(double part) {
    if (PROG.total <= 0) return;
    double elapsed = tc_time() - PROG.t0;
    if (elapsed < PROG.quiet) return;
    double frac = (PROG.done + PROG.tile * part) / PROG.total;
    if (frac > 0.999) frac = 0.999;
    if (frac < 0) frac = 0;
    int pct = (int)(frac * 100);
    if (pct < PROG.last_pct) pct = PROG.last_pct;
    double est = frac > 0.0005 ? elapsed / frac : 1e9;
    int step = est > 300.0 ? 1 : est > 60.0 ? 5 : 10;
    if (PROG.shown && pct / step <= PROG.last_pct / step) return;
    PROG.shown = 1;
    PROG.last_pct = pct;
    if (frac >= 0.02) {
        char left[48];
        fmt_left(est - elapsed, left, sizeof left);
        printf("  %d%% (%.0fs elapsed, about %s left)\n", pct, elapsed, left);
    } else {
        printf("  %d%% (%.0fs elapsed)\n", pct, elapsed);
    }
    fflush(stdout);
}

static void prog_scan(long rows) {
    double f = PROG.rowcum && rows >= 0 && rows <= PROG.rows ? PROG.rowcum[rows] : (PROG.rows > 0 ? (double)rows / PROG.rows : 0);
    prog_emit(PROG.scan_share * f);
}

static void prog_area(long started, long total) {
    double f = total > 0 ? (double)started / total : 1;
    if (f > 1) f = 1;
    prog_emit(PROG.scan_share + (1.0 - PROG.scan_share) * f);
}

static void prog_rows(const InputData *in, int rx0, int rz0, int side) {
    free(PROG.rowcum);
    PROG.rowcum = (double *)malloc(sizeof(double) * ((size_t)side + 1));
    if (!PROG.rowcum) return;
    double r = in->search_radius, half = REGION_BLOCKS * 0.5, acc = 0;
    PROG.rowcum[0] = 0;
    for (int i = 0; i < side; i++) {
        double dx = (double)(rx0 + i) * REGION_BLOCKS + half - in->center_x;
        double w = 0.02;
        if (fabs(dx) <= r + REGION_BLOCKS) {
            double h = r * r - dx * dx;
            h = h > 0 ? sqrt(h) : 0;
            double j0 = (in->center_z - h - half) / REGION_BLOCKS, j1 = (in->center_z + h - half) / REGION_BLOCKS;
            double lo = j0 > rz0 ? j0 : rz0, hi = j1 < rz0 + side ? j1 : rz0 + side;
            if (hi > lo) w += hi - lo;
        }
        acc += w;
        PROG.rowcum[i + 1] = acc;
    }
    for (int i = 0; i <= side; i++) PROG.rowcum[i] /= acc;
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
        if (worker == 0) prog_scan(rows);
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
    prog_rows(in, rx0, rz0, side);
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
    int half = (int)(sqrt((double)r->shape_w * r->shape_w + (double)r->shape_h * r->shape_h) / 2) + 1;
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

typedef struct { int mobs[MOB_COUNT], spawners, vaults, ominous; } Tally;

static void tally_blocks(const Block *all, int n, const Area *a, const Result *r, Tally *t) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < n; i++) {
        const Block *p = &all[i];
        if (a && !inside(a, r, p->x, p->z)) continue;
        if (p->kind == KIND_SPAWNER) { t->spawners++; if (p->mob >= 0) t->mobs[p->mob]++; }
        else if (p->kind == KIND_VAULT) t->vaults++;
        else if (p->kind == KIND_OMINOUS_VAULT) { t->vaults++; t->ominous++; }
    }
}

static void print_breakdown(const InputData *in, const Tally *t) {
    printf("    spawners %d (", t->spawners);
    int first = 1;
    for (int m = 0; m < MOB_COUNT; m++) {
        if (!t->mobs[m]) continue;
        printf("%s%d %s%s", first ? "" : ", ", t->mobs[m], MOB_WORDS[m], in->exclude[m] ? " [left out]" : "");
        first = 0;
    }
    printf(")  vaults %d (ominous %d)\n", t->vaults, t->ominous);
}

static FILE *csv_open(const InputData *in, const Area *ar, char *name, size_t size) {
    for (int k = 1; k < 1000; k++) {
        if (k == 1) snprintf(name, size, "%" PRId64 "_%s.csv", in->seed, COUNT_NAMES[in->count_kind]);
        else snprintf(name, size, "%" PRId64 "_%s_%d.csv", in->seed, COUNT_NAMES[in->count_kind], k);
        FILE *probe = fopen(name, "r");
        if (probe) { fclose(probe); continue; }
        FILE *f = fopen(name, "w");
        if (!f) return NULL;
        fprintf(f, "rank,counted,count,chambers,");
        if (ar->shape == SHAPE_CHAMBERS) fprintf(f, "tp,%s", ar->chambers > 1 ? "blocks_apart," : "");
        else fprintf(f, "tp,area,area_min_x,area_min_z,area_max_x,area_max_z,");
        fprintf(f, "spawners,vaults,ominous_vaults,spawner_types%s\n", ar->shape == SHAPE_CHAMBERS && ar->chambers > 1 ? ",chamber_tps" : "");
        return f;
    }
    return NULL;
}

static void csv_row(FILE *f, int rank, const InputData *in, const Area *ar, const Result *r, const int *px, const int *py, const int *pz,
                    const int *cnt, double far, const Tally *t) {
    fprintf(f, "%d,%s,%d,%d,", rank, COUNT_NAMES[in->count_kind], r->count, r->nchambers);
    if (ar->shape == SHAPE_CHAMBERS) {
        fprintf(f, "/tp %d %d %d,", px[0], py[0], pz[0]);
        if (r->nchambers > 1) fprintf(f, "%.0f,", far);
    } else {
        char area[64];
        if (ar->shape == SHAPE_SQUARE) snprintf(area, sizeof area, "square r=%d", ar->radius);
        else snprintf(area, sizeof area, "rectangle %dx%d", r->shape_w, r->shape_h);
        fprintf(f, "/tp %d ~ %d,%s,%d,%d,%d,%d,", r->x, r->z, area, r->minX, r->minZ, r->minX + r->shape_w - 1, r->minZ + r->shape_h - 1);
    }
    fprintf(f, "%d,%d,%d,", t->spawners, t->vaults, t->ominous);
    int first = 1;
    for (int m = 0; m < MOB_COUNT; m++) {
        if (!t->mobs[m]) continue;
        fprintf(f, "%s%d %s", first ? "" : " ", t->mobs[m], MOB_WORDS[m]);
        first = 0;
    }
    if (ar->shape == SHAPE_CHAMBERS && r->nchambers > 1) {
        fprintf(f, ",");
        for (int q = 0; q < r->nchambers; q++) fprintf(f, "%s%d %d %d (%d)", q ? "; " : "", px[q], py[q], pz[q], cnt[q]);
    }
    fprintf(f, "\n");
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
    int init_cut;
} AreaJob;

static int in_core(const AreaJob *job, const Ch *h) {
    int i = h->rx - job->s->rx0, j = h->rz - job->s->rz0;
    return i >= job->ci0 && i <= job->ci1 && j >= job->cj0 && j <= job->cj1;
}

static int rect_bound(const Pt *p, int n, int w, int h, Buf *scratch) {
    int minx = p[0].x, maxx = p[0].x, minz = p[0].z, maxz = p[0].z;
    for (int i = 1; i < n; i++) {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].z < minz) minz = p[i].z;
        if (p[i].z > maxz) maxz = p[i].z;
    }
    int cw = (w + 3) / 4, ch = (h + 3) / 4;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    int64_t limit = (int64_t)4 * n + 256;
    int nx, nz;
    for (;;) {
        nx = (maxx - minx) / cw + 1;
        nz = (maxz - minz) / ch + 1;
        if ((int64_t)(nx + 1) * (nz + 1) <= limit) break;
        cw *= 2;
        ch *= 2;
    }
    int *pre = (int *)buf_room(scratch, (nx + 1) * (nz + 1), sizeof(int));
    memset(pre, 0, sizeof(int) * (size_t)(nx + 1) * (nz + 1));
    for (int i = 0; i < n; i++) pre[((p[i].x - minx) / cw + 1) * (nz + 1) + (p[i].z - minz) / ch + 1]++;
    for (int i = 1; i <= nx; i++)
        for (int j = 1; j <= nz; j++) pre[i * (nz + 1) + j] += pre[(i - 1) * (nz + 1) + j] + pre[i * (nz + 1) + j - 1] - pre[(i - 1) * (nz + 1) + j - 1];
    int bw = (w + cw - 1) / cw + 1, bh = (h + ch - 1) / ch + 1, best = 0;
    if (bw > nx) bw = nx;
    if (bh > nz) bh = nz;
    for (int i = bw; i <= nx; i++) {
        for (int j = bh; j <= nz; j++) {
            int c = pre[i * (nz + 1) + j] - pre[(i - bw) * (nz + 1) + j] - pre[i * (nz + 1) + j - bh] + pre[(i - bw) * (nz + 1) + j - bh];
            if (c > best) best = c;
        }
    }
    return best;
}

static int area_result(const AreaJob *job, int a, Buf *nb, Buf *pts, Buf *seen, Result *r, int cut) {
    const InputData *in = job->in;
    const Area *ar = job->ar;
    const Scan *s = job->s;
    int m = gather(s, a, job->reach, nb, pts);
    if (m < in->min_count) return 0;
    Pt *local = (Pt *)pts->p;
    if (cut < in->min_count) cut = (int)in->min_count;
    if (m < cut) return 0;
    if (cut > 1) {
        int bnd = rect_bound(local, m, ar->side_w, ar->side_h, seen);
        if (ar->shape == SHAPE_RECTANGLE && ar->side_w != ar->side_h) {
            int b2 = rect_bound(local, m, ar->side_h, ar->side_w, seen);
            if (b2 > bnd) bnd = b2;
        }
        if (bnd < cut) return 0;
    }
    memset(r, 0, sizeof *r);
    r->arx = s->chs[a].rx;
    r->arz = s->chs[a].rz;
    {
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

    int64_t K = job->in->show;
    int track = K <= 256 && job->ar->shape != SHAPE_CHAMBERS;
    int *top_c = track ? (int *)malloc(sizeof(int) * (size_t)K) : NULL;
    unsigned *top_h = track ? (unsigned *)malloc(sizeof(unsigned) * (size_t)K) : NULL;
    int ntop = 0, cut = track ? job->init_cut : 0;
    for (;;) {
        long k = tc_atomic_next(&job->next);
        if (k >= job->nchunks) break;
        int a0 = (int)(k * ANCHOR_CHUNK), a1 = a0 + ANCHOR_CHUNK < s->nch ? a0 + ANCHOR_CHUNK : s->nch;
        for (int a = a0; a < a1; a++) {
            if (!in_core(job, &s->chs[a])) continue;
            Result r;
            int ok = job->ar->shape == SHAPE_CHAMBERS ? group_result(job, a, &r) : area_result(job, a, &nb, &pts, &seen, &r, cut);
            if (!ok) continue;
            if (track && (ntop < K || r.count >= top_c[K - 1])) {
                int dup = 0, q;
                for (q = 0; q < ntop && top_c[q] >= r.count; q++) if (top_c[q] == r.count && top_h[q] == r.hash) { dup = 1; break; }
                if (!dup && q < K) {
                    int pos = q;
                    int last = ntop < K ? ntop : (int)K - 1;
                    for (int t = last; t > pos; t--) { top_c[t] = top_c[t - 1]; top_h[t] = top_h[t - 1]; }
                    top_c[pos] = r.count; top_h[pos] = r.hash;
                    if (ntop < K) ntop++;
                    if (ntop == K && top_c[K - 1] > cut) cut = top_c[K - 1];
                }
            }
            *(Result *)buf_room(out, 1, sizeof(Result)) = r;
            if (++out->n > limit) out->n = keep_best((Result *)out->p, out->n, job->in->show);
        }
        if (worker == 0) prog_area(k + 1, job->nchunks);
    }
    free(nb.p); free(pts.p); free(seen.p); free(top_c); free(top_h);
}

static void run_tile(const InputData *in, const Area *ar, int reach, int halo, int ci0, int cj0, int ci1, int cj1,
                     Buf *all, int *nch, int *n_deep) {
    Scan s;
    memset(&s, 0, sizeof s);
    int w = (ci1 - ci0 + 1) + 2 * halo, h = (cj1 - cj0 + 1) + 2 * halo;
    double t_a = tc_time();
    PROG.phase = 0;
    scan_window(in, &s, ci0 - halo, cj0 - halo, w > h ? w : h, halo, halo, halo + (ci1 - ci0), halo + (cj1 - cj0));
    AreaJob job;
    memset(&job, 0, sizeof job);
    job.in = in; job.s = &s; job.ar = ar; job.reach = reach;
    job.init_cut = all->n >= in->show && in->show <= 256 ? ((const Result *)all->p)[in->show - 1].count : 0;
    job.ci0 = s.ci0; job.cj0 = s.cj0; job.ci1 = s.ci1; job.cj1 = s.cj1;
    job.nchunks = (s.nch + ANCHOR_CHUNK - 1) / ANCHOR_CHUNK;
    job.res = (Buf *)calloc((size_t)in->threads, sizeof(Buf));
    if (!job.res) out_of_memory();
    double t_b = tc_time();
    PROG.phase = 1;
    tc_parallel(in->threads, area_worker, &job);
    double t_c = tc_time();
    PROG.scan_time += t_b - t_a;
    PROG.area_time += t_c - t_b;
    if (PROG.scan_time + PROG.area_time > 0) PROG.scan_share = PROG.scan_time / (PROG.scan_time + PROG.area_time);
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
    char csv_name[96] = "";
    FILE *csv = in->csv && nres > 0 ? csv_open(in, ar, csv_name, sizeof csv_name) : NULL;
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
            double far_d = 0;
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
                far_d = sqrt((double)far);
                printf("Found %d %s in %d chambers, up to %.0f blocks apart\n", r->count, COUNT_WORDS[in->count_kind], n, far_d);
                for (int p = 0; p < n; p++)
                    printf("    /tp %d %d %d  (%d %s)\n", px[p], py[p], pz[p], cnt[p], COUNT_WORDS[in->count_kind]);
            }
            Tally t;
            tally_blocks((const Block *)blocks.p, blocks.n, NULL, NULL, &t);
            print_breakdown(in, &t);
            if (csv) csv_row(csv, i + 1, in, ar, r, px, py, pz, cnt, far_d, &t);
            continue;
        }
        char where[160];
        if (ar->shape == SHAPE_SQUARE)
            snprintf(where, sizeof where, "square r=%d (%d %d to %d %d)", ar->radius, r->minX, r->minZ,
                     r->minX + r->shape_w - 1, r->minZ + r->shape_h - 1);
        else
            snprintf(where, sizeof where, "%dx%d rectangle (%d %d to %d %d)", r->shape_w, r->shape_h, r->minX, r->minZ,
                     r->minX + r->shape_w - 1, r->minZ + r->shape_h - 1);
        printf("Found %d %s (%d chamber%s) in %s at /tp %d ~ %d\n", r->count, COUNT_WORDS[in->count_kind],
               r->nchambers, r->nchambers == 1 ? "" : "s", where, r->x, r->z);
        blocks_around(in, ar, r, &blocks);
        Tally t;
        tally_blocks((const Block *)blocks.p, blocks.n, ar, r, &t);
        print_breakdown(in, &t);
        if (csv) csv_row(csv, i + 1, in, ar, r, NULL, NULL, NULL, NULL, 0, &t);
    }
    if (csv) {
        fclose(csv);
        printf("\nSaved %d result%s to %s\n", nres, nres == 1 ? "" : "s", csv_name);
    } else if (in->csv && nres > 0) {
        printf("\nCould not write the CSV file\n");
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
    int extent = ar->side_w > ar->side_h ? ar->side_w : ar->side_h;
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
    double t0 = tc_time();
    PROG.total = 0;
    for (int i = i0; i <= i1; i += tile) {
        for (int j = j0; j <= j1; j += tile) {
            int ci1 = i + tile - 1 < i1 ? i + tile - 1 : i1, cj1 = j + tile - 1 < j1 ? j + tile - 1 : j1;
            PROG.total += tile_weight(in, i, j, ci1, cj1);
        }
    }
    PROG.done = 0;
    PROG.last_pct = 0;
    PROG.shown = 0;
    PROG.t0 = t0;
    PROG.quiet = PROG_QUIET_SECONDS;
    PROG.scan_share = 0.96;
    PROG.scan_time = PROG.area_time = 0;
    printf("Initiating...\n");
    fflush(stdout);
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
    printf("%d chambers in range (%d skipped: deep dark), %.0fs\n\n", nch, n_deep, tc_time() - t0);
    int nres = keep_best((Result *)all.p, all.n, in->show);
    qsort(all.p, nres, sizeof(Result), cmp_display);
    print_results(in, ar, (const Result *)all.p, nres);
    if (nres == 0) printf("Nothing found with at least %" PRId64 " %s\n", in->min_count, COUNT_WORDS[in->count_kind]);
    free(all.p);
    return 0;
}
