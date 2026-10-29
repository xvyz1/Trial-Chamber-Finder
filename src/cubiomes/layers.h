#ifndef LAYER_H_
#define LAYER_H_

#include "noise.h"
#include "biomes.h"

#define LAYER_INIT_SHA          (~0ULL)

enum BiomeTempCategory
{
    Oceanic, Warm, Lush, Cold, Freezing, Special
};

enum LayerId
{

    L_CONTINENT_4096 = 0,   L_ISLAND_4096 = L_CONTINENT_4096,
    L_ZOOM_4096,
    L_LAND_4096,
    L_ZOOM_2048,
    L_LAND_2048,            L_ADD_ISLAND_2048 = L_LAND_2048,
    L_ZOOM_1024,
    L_LAND_1024_A,          L_ADD_ISLAND_1024A = L_LAND_1024_A,
    L_LAND_1024_B,          L_ADD_ISLAND_1024B = L_LAND_1024_B,
    L_LAND_1024_C,          L_ADD_ISLAND_1024C = L_LAND_1024_C,
    L_ISLAND_1024,          L_REMOVE_OCEAN_1024 = L_ISLAND_1024,
    L_SNOW_1024,            L_ADD_SNOW_1024 = L_SNOW_1024,
    L_LAND_1024_D,          L_ADD_ISLAND_1024D = L_LAND_1024_D,
    L_COOL_1024,            L_COOL_WARM_1024 = L_COOL_1024,
    L_HEAT_1024,            L_HEAT_ICE_1024 = L_HEAT_1024,
    L_SPECIAL_1024,
    L_ZOOM_512,
    L_LAND_512,
    L_ZOOM_256,
    L_LAND_256,             L_ADD_ISLAND_256 = L_LAND_256,
    L_MUSHROOM_256,         L_ADD_MUSHROOM_256 = L_MUSHROOM_256,
    L_DEEP_OCEAN_256,
    L_BIOME_256,
    L_BAMBOO_256,           L14_BAMBOO_256 = L_BAMBOO_256,
    L_ZOOM_128,
    L_ZOOM_64,
    L_BIOME_EDGE_64,
    L_NOISE_256,            L_RIVER_INIT_256 = L_NOISE_256,
    L_ZOOM_128_HILLS,
    L_ZOOM_64_HILLS,
    L_HILLS_64,
    L_SUNFLOWER_64,         L_RARE_BIOME_64 = L_SUNFLOWER_64,
    L_ZOOM_32,
    L_LAND_32,              L_ADD_ISLAND_32 = L_LAND_32,
    L_ZOOM_16,
    L_SHORE_16,
    L_SWAMP_RIVER_16,
    L_ZOOM_8,
    L_ZOOM_4,
    L_SMOOTH_4,
    L_ZOOM_128_RIVER,
    L_ZOOM_64_RIVER,
    L_ZOOM_32_RIVER,
    L_ZOOM_16_RIVER,
    L_ZOOM_8_RIVER,
    L_ZOOM_4_RIVER,
    L_RIVER_4,
    L_SMOOTH_4_RIVER,
    L_RIVER_MIX_4,
    L_OCEAN_TEMP_256,       L13_OCEAN_TEMP_256 = L_OCEAN_TEMP_256,
    L_ZOOM_128_OCEAN,       L13_ZOOM_128 = L_ZOOM_128_OCEAN,
    L_ZOOM_64_OCEAN,        L13_ZOOM_64 = L_ZOOM_64_OCEAN,
    L_ZOOM_32_OCEAN,        L13_ZOOM_32 = L_ZOOM_32_OCEAN,
    L_ZOOM_16_OCEAN,        L13_ZOOM_16 = L_ZOOM_16_OCEAN,
    L_ZOOM_8_OCEAN,         L13_ZOOM_8 = L_ZOOM_8_OCEAN,
    L_ZOOM_4_OCEAN,         L13_ZOOM_4 = L_ZOOM_4_OCEAN,
    L_OCEAN_MIX_4,          L13_OCEAN_MIX_4 = L_OCEAN_MIX_4,

    L_VORONOI_1,            L_VORONOI_ZOOM_1 = L_VORONOI_1,

    L_ZOOM_LARGE_A,
    L_ZOOM_LARGE_B,
    L_ZOOM_L_RIVER_A,
    L_ZOOM_L_RIVER_B,

    L_NUM
};

struct Layer;
typedef int (mapfunc_t)(const struct Layer *, int *, int, int, int, int);

STRUCT(Layer)
{
    mapfunc_t *getMap;

    int8_t mc;
    int8_t zoom;
    int8_t edge;
    int scale;

    uint64_t layerSalt;
    uint64_t startSalt;
    uint64_t startSeed;

    void *noise;
    void *data;

    Layer *p, *p2;
};

STRUCT(LayerStack)
{
    Layer layers[L_NUM];
    Layer *entry_1;
    Layer *entry_4;

    Layer *entry_16;
    Layer *entry_64;
    Layer *entry_256;
    PerlinNoise oceanRnd;
};

#ifdef __cplusplus
extern "C"
{
#endif

void setLayerSeed(Layer *layer, uint64_t worldSeed);

mapfunc_t mapContinent;
mapfunc_t mapZoomFuzzy;
mapfunc_t mapZoom;
mapfunc_t mapLand;
mapfunc_t mapLand16;
mapfunc_t mapLandB18;
mapfunc_t mapIsland;
mapfunc_t mapSnow;
mapfunc_t mapSnow16;
mapfunc_t mapCool;
mapfunc_t mapHeat;
mapfunc_t mapSpecial;
mapfunc_t mapMushroom;
mapfunc_t mapDeepOcean;
mapfunc_t mapBiome;
mapfunc_t mapBamboo;
mapfunc_t mapNoise;
mapfunc_t mapBiomeEdge;
mapfunc_t mapHills;
mapfunc_t mapRiver;
mapfunc_t mapSmooth;
mapfunc_t mapSunflower;
mapfunc_t mapShore;
mapfunc_t mapSwampRiver;
mapfunc_t mapRiverMix;
mapfunc_t mapOceanTemp;
mapfunc_t mapOceanMix;

mapfunc_t mapVoronoi;
mapfunc_t mapVoronoi114;

ATTR(const)
uint64_t getVoronoiSHA(uint64_t worldSeed);
void voronoiAccess3D(uint64_t sha, int x, int y, int z, int *x4, int *y4, int *z4);

void mapVoronoiPlane(uint64_t sha, int *out, int *src,
    int x, int z, int w, int h, int y, int px, int pz, int pw, int ph);

#ifdef __cplusplus
}
#endif

#endif
