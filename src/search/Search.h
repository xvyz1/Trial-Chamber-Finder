#ifndef SEARCH_H
#define SEARCH_H

#include <stdint.h>

enum { COUNT_SPAWNERS, COUNT_VAULTS, COUNT_OMINOUS, COUNT_KINDS };
enum { SHAPE_CHAMBERS, SHAPE_SQUARE, SHAPE_RECTANGLE, SHAPE_CIRCLE, SHAPE_COUNT };
enum { SORT_AMOUNT, SORT_CLOSENESS, SORT_AMOUNT_THEN_CLOSENESS, SORT_COUNT };
enum { MOB_ZOMBIE, MOB_HUSK, MOB_SPIDER, MOB_SLIME, MOB_CAVE_SPIDER, MOB_SILVERFISH, MOB_BABY_ZOMBIE,
       MOB_SKELETON, MOB_STRAY, MOB_BOGGED, MOB_BREEZE, MOB_COUNT };

extern const char *const COUNT_NAMES[COUNT_KINDS];
extern const char *const SHAPE_NAMES[SHAPE_COUNT];
extern const char *const MOB_NAMES[MOB_COUNT];
extern const char *const SORT_NAMES[SORT_COUNT];

typedef struct {
    int shape;
    int radius;
    int side_w, side_h;
    int chambers;
} Area;

typedef struct {
    int64_t seed;
    int count_kind;
    Area area;
    int exclude[MOB_COUNT];
    int64_t min_count, show;
    int search_radius, center_x, center_z;
    int threads;
    int version;
    int sort;
} InputData;

int search_run(const InputData *in);

#endif
