#ifndef BIOMENOISE_H_
#define BIOMENOISE_H_

#include "noise.h"
#include "layers.h"

STRUCT(Range)
{

    int scale;
    int x, z, sx, sz;
    int y, sy;
};

STRUCT(NetherNoise)
{

    DoublePerlinNoise temperature;
    DoublePerlinNoise humidity;
    PerlinNoise oct[8];
};

STRUCT(EndNoise)
{
    PerlinNoise perlin;
    int mc;
};

STRUCT(SurfaceNoise)
{
    double xzScale, yScale;
    double xzFactor, yFactor;
    OctaveNoise octmin;
    OctaveNoise octmax;
    OctaveNoise octmain;
    OctaveNoise octsurf;
    OctaveNoise octdepth;
    PerlinNoise oct[16+16+8+4+16];
};

STRUCT(SurfaceNoiseBeta)
{
    OctaveNoise octmin;
    OctaveNoise octmax;
    OctaveNoise octmain;
    OctaveNoise octcontA;
    OctaveNoise octcontB;
    PerlinNoise oct[16+16+8+10+16];
};

STRUCT(SeaLevelColumnNoiseBeta)
{
    double contASample;
    double contBSample;
    double minSample[2];
    double maxSample[2];
    double mainSample[2];
};

STRUCT(Spline)
{
    int len, typ;
    float loc[12];
    float der[12];
    Spline *val[12];
};

STRUCT(FixSpline)
{
    int len;
    float val;
};

STRUCT(SplineStack)
{
    Spline stack[42];
    FixSpline fstack[151];
    int len, flen;
};

enum
{
    NP_TEMPERATURE      = 0,
    NP_HUMIDITY         = 1,
    NP_CONTINENTALNESS  = 2,
    NP_EROSION          = 3,
    NP_SHIFT            = 4, NP_DEPTH = NP_SHIFT,
    NP_WEIRDNESS        = 5,
    NP_MAX
};

STRUCT(BiomeNoise)
{
    DoublePerlinNoise climate[NP_MAX];
    PerlinNoise oct[2*23];
    Spline *sp;
    SplineStack ss;
    int nptype;
    int mc;
};

STRUCT(BiomeNoiseBeta)
{
    OctaveNoise climate[3];
    PerlinNoise oct[10];
    int nptype;
    int mc;
};

STRUCT(BiomeTree)
{
    const uint32_t *steps;
    const int32_t  *param;
    const uint64_t *nodes;
    uint32_t order;
    uint32_t len;
};

#ifdef __cplusplus
extern "C"
{
#endif

void initSurfaceNoise(SurfaceNoise *sn, int dim, uint64_t seed);
void initSurfaceNoiseBeta(SurfaceNoiseBeta *snb, uint64_t seed);
double sampleSurfaceNoise(const SurfaceNoise *sn, int x, int y, int z);
double sampleSurfaceNoiseBetween(const SurfaceNoise *sn, int x, int y, int z,
    double noiseMin, double noiseMax);

void setNetherSeed(NetherNoise *nn, uint64_t seed);
int getNetherBiome(const NetherNoise *nn, int x, int y, int z, float *ndel);
int mapNether2D(const NetherNoise *nn, int *out, int x, int z, int w, int h);
int mapNether3D(const NetherNoise *nn, int *out, Range r, float confidence);

int genNetherScaled(const NetherNoise *nn, int *out, Range r, int mc, uint64_t sha);

void setEndSeed(EndNoise *en, int mc, uint64_t seed);
int mapEndBiome(const EndNoise *en, int *out, int x, int z, int w, int h);
int mapEnd(const EndNoise *en, int *out, int x, int z, int w, int h);
int getEndSurfaceHeight(int mc, uint64_t seed, int x, int z);
int mapEndSurfaceHeight(float *y, const EndNoise *en, const SurfaceNoise *sn,
    int x, int z, int w, int h, int scale, int ymin);

int genEndScaled(const EndNoise *en, int *out, Range r, int mc, uint64_t sha);

enum {
    SAMPLE_NO_SHIFT = 0x1,
    SAMPLE_NO_DEPTH = 0x2,
    SAMPLE_NO_BIOME = 0x4,
};
void initBiomeNoise(BiomeNoise *bn, int mc);
void setBiomeSeed(BiomeNoise *bn, uint64_t seed, int large);
void setBetaBiomeSeed(BiomeNoiseBeta *bnb, uint64_t seed);
int sampleBiomeNoise(const BiomeNoise *bn, int64_t *np, int x, int y, int z,
    uint64_t *dat, uint32_t sample_flags);
int sampleBiomeNoiseBeta(const BiomeNoiseBeta *bnb, int64_t *np, double *nv,
    int x, int z);
double approxSurfaceBeta(const BiomeNoiseBeta *bnb, const SurfaceNoiseBeta *snb,
    int x, int z);

int getOldBetaBiome(float t, float h);

int climateToBiome(int mc, const uint64_t np[6], uint64_t *dat);

void setClimateParaSeed(BiomeNoise *bn, uint64_t seed, int large, int nptype, int nmax);
double sampleClimatePara(const BiomeNoise *bn, int64_t *np, double x, double z);

void genBiomeNoiseChunkSection(const BiomeNoise *bn, int out[4][4][4],
    int cx, int cy, int cz, uint64_t *dat);

int genBiomeNoiseScaled(const BiomeNoise *bn, int *out, Range r, uint64_t sha);

int genBiomeNoiseBetaScaled(const BiomeNoiseBeta *bnb, const SurfaceNoiseBeta *snb,
    int *out, Range r);

int getBiomeDepthAndScale(int id, double *depth, double *scale, int *grass);

Range getVoronoiSrcRange(Range r);

#ifdef __cplusplus
}
#endif

#endif
