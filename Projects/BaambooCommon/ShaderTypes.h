#pragma once
#include "MathTypes.h"

struct TransformData
{
    mat4 mWorldToView;
    mat4 mViewToWorld;
};

struct BoneTransformData
{
    mat4 mBones[MAX_BONES];
};

struct MaterialData
{
    float3 tint;
    float  metallic;
    float  roughness;
    float  ior;
    float  emissivePower;
    float  padding0;

    u32 albedoID;
    u32 normalID;
    u32 specularID;
    u32 metallicRoughnessAoID;
    u32 emissionID;

    float3 padding1;
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


struct DirectionalLight
{
    float3 direction;
    float  illuminanceLux;
    float3 color;
    float  angularRadiusRad;
    float  temperatureK;
    float3 padding;
};

struct PointLight
{
    float3 position;
    float  luminousFluxLm;
    float3 color;
    float  radiusM;
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

#define MAX_DIRECTIONAL_LIGHT 2
#define MAX_POINT_LIGHT       512
#define MAX_SPOT_LIGHT        32
struct LightData
{
    DirectionalLight directionals[MAX_DIRECTIONAL_LIGHT];
    PointLight       points[MAX_POINT_LIGHT];
    SpotLight        spots[MAX_SPOT_LIGHT];

    u32   numDirectionals;
    u32   numPoints;
    u32   numSpots;
    float padding0;

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
    float  topLayerKm;
    float  bottomLayerKm;
    float2 padding0;

    float3 extinctionStrength;
    float  extinctionScale;

    float msContribution;
    float msOcclusion;
    float msEccentricity;
    float groundContributionStrength;

    float coverage;
    float cloudType;
    float baseNoiseScale;
    float baseIntensity;

    float erosionNoiseScale;
    float erosionIntensity;
    float erosionPower;
    float wispiness;
    float billowiness;
    float precipitation;
    float erosionHeightGradientMultiplier;
    float erosionHeightGradientPower;

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
    mat4 mSunViewProj;
    mat4 mSunViewProjInv;
};