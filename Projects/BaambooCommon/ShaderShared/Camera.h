#pragma once
#include "Types.h"

struct CameraData
{
    mat4 mView;
    mat4 mProj;
    mat4 mViewProj;
    mat4 mViewProjInv;
    mat4 mViewProjUnjittered;
    mat4 mViewProjUnjitteredPrev;

    float3 position;
    float  padding0;
    float2 jitterUV;
    float  zNear;
    float  zFar;
};

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

#ifdef __cplusplus
static_assert(sizeof(CameraData) == 416);
static_assert(sizeof(CullData) == 6 * sizeof(float4) + 32);
#endif
