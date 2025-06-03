#pragma once
#include "MathTypes.h"

struct TransformData
{
    mat4 mWorldToView;
    mat4 mViewToWorld;
};

struct MaterialData
{
    float3 tint;
    float  metallic;
    float  roughness;
    float3 padding0;

    u32 albedoID;
    u32 normalID;
    u32 specularID;
    u32 aoID;
    u32 roughnessID;
    u32 metallicID;
    u32 emissionID;
    u32 padding1;
};

struct CameraData
{
    mat4 mView;
    mat4 mProj;
    mat4 mViewProj;

    float3 position;
    float  padding0;
};