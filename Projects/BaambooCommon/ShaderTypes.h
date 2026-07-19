#pragma once
#include "MathTypes.h"
#include "ComponentTypes.h"

struct MeshLOD
{
    u32 iOffset;

    u32 mCount;
    u32 mOffset;
    u32 mvOffset;
    u32 mtOffset;

    float simplifyError; // for lod scale
};

struct MeshData
{
    u32 vOffset;

    float3 center;
    float  radius;

    u32     maxLOD;
    MeshLOD lods[LOD_COUNT];
};
static_assert(sizeof(MeshLOD) == 24);
static_assert(sizeof(MeshData) == 4 + 12 + 4 + 4 + LOD_COUNT * sizeof(MeshLOD));

struct InstanceData
{
    u32 meshID;
    u32 transformID;
    u32 materialID;

    u32 visOffset;
    u32 isVoxel;
};

// Voxel chunk i lives at instance (kVoxelChunkInstanceBase + i) and VoxelChunkCounts[i]
constexpr u32 kVoxelChunkInstanceBase = 0u;

struct IndirectCommandData
{
    u32 drawID;

    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};


struct TransformData
{
    mat4 mLocalToWorld;
    mat4 mWorldToLocal;
};

struct BoneTransformData
{
    mat4 mBoneTransform;
};
static_assert(sizeof(BoneTransformData) == 64);


struct CameraData
{
    mat4 mView;
    mat4 mProj;
    mat4 mViewProj;
    mat4 mViewProjInv;
    mat4 mViewProjUnjittered;
    mat4 mViewProjUnjitteredPrev;

    float3 position;
    float  zNear;
    float  zFar;
    float3 padding0;
};
static_assert(sizeof(CameraData) == 416);


// Frozen camera — debug purpose
struct FrozenCameraData
{
    mat4 mView;
    mat4 mProj;
    mat4 mViewProj;
    mat4 mViewProjInv;

    float3 position;
    float  zNear;
    float2 viewport;
    float  zFar;
    float  padding0;
};


struct CullData
{
    float4 frustum[6];

    float sseThresholdPx; // for LOD selection in culling
    float viewportHeight;
    u32   cullFlags;
    u32   hiZMipCount;

    u32 hiZWidth;
    u32 hiZHeight;
    u32 padding0;
    u32 padding1;
};
static_assert(sizeof(CullData) == 6 * sizeof(float4) + 32);


struct MaterialData
{
    float3 tint;
    float  metallic;

    float roughness;
    float ior;
    float alphaCutoff;
    float padding0;

    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float anisotropyRotation;

    float3 emissionColor;
    float  emissivePower;

    float3 specularColor;
    float  specularStrength;

    float3 sheenColor;
    float  sheenRoughness;

    float subsurface;
    float transmission;
    u32   materialType;
    u32   materialFlags;

    u32 albedoID;
    u32 normalID;
    u32 specularID;
    u32 metallicRoughnessAoID;

    u32 emissiveID;
    u32 clearcoatID;
    u32 sheenID;
    u32 anisotropyID;

    u32 subsurfaceID;
    u32 transmissionID;
    u32 padding2;
    u32 padding3;
};

static constexpr u32 MATERIAL_FLAG_FACE_NORMALS = 1u << 0u;

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
static_assert(sizeof(ClusterAABB) == 32);

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


struct AtmosphereData
{
    DirectionalLight light;

    float  planetRadiusKm;
    float  atmosphereRadiusKm;
    float2 padding0;

    float3 rayleighScattering;
    float  rayleighDensityKm;

    float mieScattering;
    float mieAbsorption;
    float mieDensityKm;
    float miePhaseG;

    float3 ozoneAbsorption;
    float  ozoneCenterKm;

    float3 groundAlbedo;
    float  ozoneWidthKm;
};

struct CloudData
{
    float topLayerKm;
    float bottomLayerKm;
    float localOvercast;
    float shadowTracingDistanceKm;

    float cloudsScale;
    float clumpsVariation;
    float floorVariationClear;
    float floorVariationCloudy;

    float3 baseErosionScale;
    float  baseDensity;

    float baseErosionStrength;
    float baseErosionPower;
    float hfErosionStrength;
    float hfErosionDistortion;

    float groundContributionStrength;
    float extinctionScale;
    float msContribution;
    float msOcclusion;

    float3 windDirection;
    float  windSpeedMps;

    float ambientIntensity;
    float padding0;
    float padding1;
    float padding2;
};
static_assert(sizeof(CloudData) == 112, "CloudData layout must match Common.hlsli / DescriptorCommon.hg");
struct SceneEnvironmentData
{
    AtmosphereData atmosphere;
    CloudData      cloud;
};

struct CloudShadowData
{
    mat4 mSunView;
    mat4 mSunViewInv;
    mat4 mSunViewProj;
    mat4 mSunViewProjInv;
};


// =========================================================================
// Voxel Chunk
// =========================================================================
struct VoxelChunkDesc
{
    float3 originWS;
    u32    vertexOffset;

    u32   meshletVertexOffset;
    u32   meshletTriangleOffset;
    float chunkSizeMeter;
    u32   diceMaxLevel;          // micro-dicing max subdivision level (0 = off, 1..5)

    // Micro-dicing
    float diceRadiusMeter;
    float diceFadeWidthMeter;    // displacement fades to 0 approaching the radius
    float diceDisplacementScale;
    u32   debugFlags;            // bit0 = dice-level tint

    // Adaptive dicing
    float diceTargetPx;        // target sub-edge screen size (px)
    float diceKScale;          // K = 0.5 * viewportH * |P11|
    float voxelSizeMeter;
    float microAmplitudeMeter;

    float microBaseWaveLengthMeter; // largest micro octave wavelength
    float microLacunarity;
    float microGain;
    float microCreaseBoost;         // erosion crease/ridge amplitude modulation strength

    u32   microOctaves;
    float microSharpness; // -1 = ridged .. 0 = plain .. +1 = billowed
    u32   padding2;
    u32   padding3;
};
static_assert(sizeof(VoxelChunkDesc) == 96);

struct VoxelChunkCounts
{
    u32 meshletCount;
};
static_assert(sizeof(VoxelChunkCounts) == 4);

// =========================================================================
// Voxel Terrain Generation Params
// =========================================================================
struct VoxelTerrainGenParams
{
    float3 chunkOriginWS;
    float  voxelSizeMeter;

    u32 cellsPerAxis;
    u32 samplesPerAxis;
    u32 apron;
    u32 seed;

    float frequency;
    u32   octaves;
    float lacunarity;
    float gain;

    float warpStrength;
    float warpFrequency;
    float mountainAmplitude;
    float detailWeight;

    float redistributionExp;
    float ridgedBlend;
    float surfaceLevelRatio;
    float erosionScale;

    float erosionStrength;
    float erosionGullyWeight;
    float erosionDetail;
    float erosionOnsetInput;
          
    float erosionOnsetOctave;
    float erosionCellScale;
    float erosionNormalization;
    float erosionSlopeScale;

    u32 erosionOctaves;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};
static_assert(sizeof(VoxelTerrainGenParams) == 128);
