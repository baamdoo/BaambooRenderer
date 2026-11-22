#pragma once
#include "Types.h"

struct DirectionalLight
{
    float3 direction;
    float  illuminanceLux;
    float3 color;
    float  angularRadiusRad;
    float  temperatureK;
    float3 padding;
};

struct SpotLight
{
    float3 position;
    float  luminousFluxLm;
    float3 direction;
    float  radiusM;
    float3 color;
    float  innerConeAngleRad;
    float  outerConeAngleRad;
    float  temperatureK;
    float2 padding;
};

struct AreaLight
{
    float3 position;
    float  halfWidth;
    float3 normal;
    float  halfHeight;
    float3 tangent;
    float  luminousFluxLm;
    float3 color;
    float  temperatureK;
};

struct SphereLight
{
    float3 position;
    float  radius;
    float3 color;
    float  luminousFluxLm;
    float  temperatureK;
    float3 padding;
};

struct DiskLight
{
    float3 position;
    float  radius;
    float3 normal;          // back-face direction (single-sided cull)
    float  luminousFluxLm;
    float3 tangent;         // azimuth zero axis on disk plane
    float  temperatureK;
    float3 color;
    float  padding;
};

struct TubeLight
{
    float3 positionA;       // line endpoint A
    float  radius;          // tube thickness (0 = ideal line)
    float3 positionB;       // line endpoint B
    float  luminousFluxLm;
    float3 color;
    float  temperatureK;
};

// =========================================================================
// Clustered Lighting (Cluster AABB Build)
// =========================================================================
struct ClusterAABB
{
    float4 aabbMin; // xyz = view-space min, w = padding (or cluster index encoding)
    float4 aabbMax; // xyz = view-space max, w = padding
};

#define CLUSTER_TILE_SIZE_PX 64u
#define CLUSTER_SLICES_Z     32u
#define MAX_CLUSTER_X        60u  // 4K viewport upper bound (3840 / 64 = 60)
#define MAX_CLUSTER_Y        34u  // 4K viewport upper bound (ceil(2160 / 64) = 34)
#define MAX_CLUSTER_Z        32u
#define MAX_CLUSTER_COUNT    (MAX_CLUSTER_X * MAX_CLUSTER_Y * MAX_CLUSTER_Z) // 65,280

#define MAX_DIRECTIONAL_LIGHT 2
#define MAX_SPOT_LIGHT        32
#define MAX_AREA_LIGHT        16
#define MAX_SPHERE_LIGHT      16
#define MAX_DISK_LIGHT        16
#define MAX_TUBE_LIGHT        16

// =========================================================================
// Light Culling
// =========================================================================
#define MAX_LIGHTS_PER_CLUSTER 64u

#define LIGHT_TYPE_BITS  3u
#define LIGHT_INDEX_BITS 29u
#define LIGHT_INDEX_MASK 0x1FFFFFFFu

#define LIGHT_TYPE_DIRECTIONAL 1u
#define LIGHT_TYPE_SPOT        2u
#define LIGHT_TYPE_SPHERE      3u
#define LIGHT_TYPE_DISK        4u
#define LIGHT_TYPE_TUBE        5u
#define LIGHT_TYPE_AREA        6u

struct LightData
{
    DirectionalLight directionals[MAX_DIRECTIONAL_LIGHT];
    SpotLight        spots      [MAX_SPOT_LIGHT];
    AreaLight        areas      [MAX_AREA_LIGHT];
    SphereLight      spheres    [MAX_SPHERE_LIGHT];
    DiskLight        disks      [MAX_DISK_LIGHT];
    TubeLight        tubes      [MAX_TUBE_LIGHT];

    u32 numDirectionals;
    u32 numSpots;
    u32 numAreas;
    u32 numSpheres;
    u32 numDisks;
    u32 numTubes;
    u32 padding0;
    u32 padding1;
};

// =========================================================================
// LightData byte layout
// =========================================================================
// LightData is larger than D3D12's 2048-byte structure stride, so shaders read it as a ByteAddressBuffer at these offsets
static const u32 kDirectionalLightStride = 48u;
static const u32 kSpotLightStride        = 64u;
static const u32 kAreaLightStride        = 64u;
static const u32 kSphereLightStride      = 48u;
static const u32 kDiskLightStride        = 64u;
static const u32 kTubeLightStride        = 48u;

static const u32 kLightDataOffsetDirectionals = 0u;
static const u32 kLightDataOffsetSpots        = kLightDataOffsetDirectionals + MAX_DIRECTIONAL_LIGHT * kDirectionalLightStride;
static const u32 kLightDataOffsetAreas        = kLightDataOffsetSpots        + MAX_SPOT_LIGHT        * kSpotLightStride;
static const u32 kLightDataOffsetSpheres      = kLightDataOffsetAreas        + MAX_AREA_LIGHT        * kAreaLightStride;
static const u32 kLightDataOffsetDisks        = kLightDataOffsetSpheres      + MAX_SPHERE_LIGHT      * kSphereLightStride;
static const u32 kLightDataOffsetTubes        = kLightDataOffsetDisks        + MAX_DISK_LIGHT        * kDiskLightStride;
static const u32 kLightDataOffsetCounts       = kLightDataOffsetTubes        + MAX_TUBE_LIGHT        * kTubeLightStride;

// The counts at the end of LightData, loaded in one read
struct LightCounts
{
    u32 numDirectionals;
    u32 numSpots;
    u32 numAreas;
    u32 numSpheres;
    u32 numDisks;
    u32 numTubes;
    u32 padding0;
    u32 padding1;
};

#ifdef __cplusplus
static_assert(sizeof(ClusterAABB) == 32);

static_assert(offsetof(LightData, directionals) == kLightDataOffsetDirectionals);
static_assert(offsetof(LightData, spots)        == kLightDataOffsetSpots);
static_assert(offsetof(LightData, areas)        == kLightDataOffsetAreas);
static_assert(offsetof(LightData, spheres)      == kLightDataOffsetSpheres);
static_assert(offsetof(LightData, disks)        == kLightDataOffsetDisks);
static_assert(offsetof(LightData, tubes)        == kLightDataOffsetTubes);
static_assert(offsetof(LightData, numDirectionals) == kLightDataOffsetCounts + offsetof(LightCounts, numDirectionals));
static_assert(offsetof(LightData, numSpots)        == kLightDataOffsetCounts + offsetof(LightCounts, numSpots));
static_assert(offsetof(LightData, numAreas)        == kLightDataOffsetCounts + offsetof(LightCounts, numAreas));
static_assert(offsetof(LightData, numSpheres)      == kLightDataOffsetCounts + offsetof(LightCounts, numSpheres));
static_assert(offsetof(LightData, numDisks)        == kLightDataOffsetCounts + offsetof(LightCounts, numDisks));
static_assert(offsetof(LightData, numTubes)        == kLightDataOffsetCounts + offsetof(LightCounts, numTubes));
static_assert(sizeof(LightData) == kLightDataOffsetCounts + sizeof(LightCounts) && sizeof(LightData) == 5760);
#endif

// =========================================================================
// Clustered lighting pass constants
// =========================================================================
// Tile size and slice count are CLUSTER_TILE_SIZE_PX and CLUSTER_SLICES_Z
struct ClusterGridConstants
{
    u32 numTilesX;
    u32 numTilesY;
    u32 padding0;
    u32 padding1;
};

struct DeferredLightingConstants
{
    u32 debugView; // 0 lit | 1 VisID class | 2 cached N | 3 live N | 4 base color | 5 roughness | 6 AO | 7 barycentrics
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

#ifdef __cplusplus
static_assert(sizeof(ClusterGridConstants) == 16);
static_assert(sizeof(DeferredLightingConstants) == 16);
#endif
