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
    float2 padding0;

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

    float3 position;
    float  padding0;
};


struct DirectionalLight
{
    float3 direction;
    float  illuminance_lux;
    float3 color;
    float  angularRadius_rad;
    float  temperature_K;
    float3 padding;
};

struct PointLight
{
    float3 position;
    float  luminousPower_lm;
    float3 color;
    float  radius_m;
    float  temperature_K;
    float3 padding;
};

struct SpotLight
{
    float3 position;
    float  luminousPower_lm;
    float3 direction;
    float  radius_m;
    float3 color;
    float  innerConeAngle_rad; 
    float  outerConeAngle_rad;
    float  temperature_K;
    float2 padding;
};

#define MAX_DIRECTIONAL_LIGHT 4
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

    float  ambientIntensity;
    float3 ambientColor;
    float  padding0;
};
