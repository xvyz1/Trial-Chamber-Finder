#ifndef CHAMBER_GENERATOR_H
#define CHAMBER_GENERATOR_H
#include <stdint.h>

#define TC_MAX_PIECES 4096

typedef struct { int minX, minY, minZ, maxX, maxY, maxZ; } TcBox;

typedef struct {
    int tpl;
    int rot;
    int x, y, z;
    TcBox box;
    int depth;
} TcPiece;

const char *tc_mob_name(int group, int i);
int tc_region_blocks(void);
int tc_deep_dark_allowed(void);

#define TC_MAX_BLOCKS 1024

typedef struct {
    int x, y, z;
    int kind;
    int tpl;
} TcBlock;

typedef struct {
    int64_t seed;
    int chunkX, chunkZ;
    int startY;
    int posX, posY, posZ;
    int ranged, melee, small_melee;
    int npieces;
    int spawners, vaults, ominous;
    int nblocks;
    TcBlock blocks[TC_MAX_BLOCKS];
    TcPiece pieces[TC_MAX_PIECES];
} TcChamber;

void tc_start_pos(int64_t seed, int chunkX, int chunkZ, int *posX, int *posY, int *posZ);

void tc_init(void);
int tc_num_versions(void);
const char *tc_version_name(int v);
int tc_version_index(const char *name);
void tc_select_version(int v);
int tc_selected_version(void);

void tc_candidate(int64_t seed, int rx, int rz, int *chunkX, int *chunkZ);

int tc_generate(TcChamber *c, int64_t seed, int chunkX, int chunkZ);

typedef struct TcWork TcWork;
TcWork *tc_work_new(void);
void tc_work_free(TcWork *w);
int tc_generate_w(TcWork *w, TcChamber *c, int64_t seed, int chunkX, int chunkZ);

int tc_selftest(long trials);

const char *tc_template_name(int tpl);
int tc_num_templates(void);
const char *tc_rotation_name(int rot);

#endif
