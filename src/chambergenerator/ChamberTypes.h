#ifndef CHAMBER_TYPES_H
#define CHAMBER_TYPES_H

enum { DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST };

enum { ROT_NONE, ROT_CW90, ROT_180, ROT_CCW90 };

enum { KIND_NONE, KIND_SPAWNER, KIND_VAULT, KIND_OMINOUS_VAULT };

typedef struct {
    short x, y, z;
    unsigned char facing, top;
    unsigned char rollable;
    signed char sel, place;
    short name, target;
    short pool;
} TcJigsaw;

typedef struct {
    const char *name;
    short sx, sy, sz;
    short jig_off, jig_n;
    unsigned char kind;
    short bx, by, bz;
} TcTemplate;

typedef struct {
    const char *name;
    short fallback;
    int off, n;
} TcPool;

typedef struct {
    int num_templates, num_pools, num_pool_ids, num_names, num_jigsaws;
    int pool_empty, pool_start;
    int alias_ranged, alias_slow_ranged, alias_melee, alias_small_melee;
    short ranged[3], slow_ranged[3], melee[3], small_melee[4];
    const TcJigsaw *jigsaws;
    const TcTemplate *templates;
    const short *pool_elements;
    const TcPool *pools;
    int spacing, separation, min_y, max_y, deep_dark_ok;
    const char *ranged_name[3], *melee_name[3], *small_melee_name[4];
} TcDataSet;

#endif
