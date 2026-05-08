#pragma once
#include "MathTypes.h"

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
};

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
    mat4 mBones[MAX_BONES];
};


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


struct CullData
{
    float4 frustum[6];

	float sseThresholdPx; // for lod selection in culling and mesh shading
    float viewportHeight;
    float padding0;
    float padding1;
};
static_assert(sizeof(CullData) == 6 * sizeof(float4) + 16);


struct MaterialData
{
    float3 tint;
    float  metallic;

    float roughness;
    float ior;
    float emissivePower;
    float alphaCutoff;

    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float anisotropyRotation;

    float3 sheenColor;
    float  sheenRoughness;

    float subsurface;
    float transmission;
    float specularStrength;
    u32   materialType;

    u32 albedoID;
    u32 normalID;
    u32 specularTexID;
    u32 metallicRoughnessAoID;

    u32 emissiveID;
    u32 clearcoatID;
    u32 sheenID;
    u32 anisotropyID;

    u32 subsurfaceID;
    u32 transmissionID;
    u32 padding0;
    u32 padding1;
};
static_assert(sizeof(MaterialData) == 128);


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

#define MAX_DIRECTIONAL_LIGHT 2
#define MAX_SPOT_LIGHT        32
#define MAX_AREA_LIGHT        16
#define MAX_SPHERE_LIGHT      16
#define MAX_DISK_LIGHT        16
#define MAX_TUBE_LIGHT        16
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

    float3 ambientColor;
    float  ambientIntensity;
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

    float3 cloudAlbedo;
    float  groundContributionStrength;

    float cloudsScale;
    float clumpsVariation;
    float floorVariationClear;
    float floorVariationCloudy;

    float3 baseErosionScale;
    float  baseDensity;
    float  baseErosionStrength;
    float  baseErosionPower;
    float  hfErosionStrength;
    float  hfErosionDistortion;

    float3 scatteringScale;
    float  extinctionScale;

    float msContribution;
    float msOcclusion;
    float msEccentricity;
    float silverScatterG;

    float ambientIntensity;
    float ambientSaturation;
    float topAmbientScale;
    float bottomAmbientScale;

    float3 windDirection;
    float  windSpeedMps;
    
};
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